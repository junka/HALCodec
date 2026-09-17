#include "session.h"

#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

namespace halcodec {

namespace {

// Aggregated (frame, stream index) entry produced by the per-stream drainer
// threads and consumed by getAny*Frame().
struct Aggregated {
    CodecFrame frame;
    size_t stream;
};

// A fan-in queue fed by one drainer thread per stream. Each drainer blocks on
// its backend's (blocking) GetFrame; when a frame arrives it is pushed here,
// and when GetFrame returns false the drainer records completion and exits.
// getAny*Frame() blocks on the queue's condition variable until either a frame
// is available or every stream has finished.
class Aggregator {
public:
    Aggregator(size_t n) : remaining_(n) {}

    void push(CodecFrame f, size_t stream) {
        {
            std::lock_guard<std::mutex> lk(mu_);
            q_.push({std::move(f), stream});
        }
        cv_.notify_one();
    }

    void streamFinished() {
        std::lock_guard<std::mutex> lk(mu_);
        if (remaining_ > 0) {
            --remaining_;
        }
        cv_.notify_all();
    }

    // Returns false only when the queue is empty AND every stream has finished.
    bool pop(CodecFrame& out, size_t& outStream) {
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait(lk, [this] { return !q_.empty() || remaining_ == 0; });
        if (q_.empty()) {
            return false; // all streams finished, nothing left
        }
        Aggregated a = std::move(q_.front());
        q_.pop();
        out = std::move(a.frame);
        outStream = a.stream;
        return true;
    }

private:
    std::mutex mu_;
    std::condition_variable cv_;
    std::queue<Aggregated> q_;
    size_t remaining_; // streams still draining (not yet finished)
};

} // namespace

// PIMPL holds the aggregators + their drainer threads so the header stays free
// of <thread>/<mutex>.
class Session::Impl {
public:
    std::vector<std::unique_ptr<Decoder>> decoders;
    std::vector<std::unique_ptr<Encoder>> encoders;

    // Fan-in state, lazily created on first getAny* call.
    std::unique_ptr<Aggregator> decAgg;
    std::unique_ptr<Aggregator> encAgg;
    std::vector<std::thread> decDrainers;
    std::vector<std::thread> encDrainers;
    bool decAggStarted = false;
    bool encAggStarted = false;

    ~Impl() {
        // Drainer threads block on backend GetFrame; the backends' own
        // destructors (run via close() / unique_ptr reset) signal EOF and join
        // their internal workers, which unblocks GetFrame -> drainers exit.
        for (auto& t : decDrainers) {
            if (t.joinable()) t.join();
        }
        for (auto& t : encDrainers) {
            if (t.joinable()) t.join();
        }
    }

    void startDecodeAggregator() {
        if (decAggStarted) return;
        decAggStarted = true;
        decAgg = std::make_unique<Aggregator>(decoders.size());
        for (size_t i = 0; i < decoders.size(); i++) {
            Decoder* d = decoders[i].get();
            Aggregator* agg = decAgg.get();
            decDrainers.emplace_back([d, agg, i] {
                CodecFrame f;
                while (d->GetFrame(f)) {
                    agg->push(std::move(f), i);
                }
                agg->streamFinished();
            });
        }
    }

    void startEncodeAggregator() {
        if (encAggStarted) return;
        encAggStarted = true;
        encAgg = std::make_unique<Aggregator>(encoders.size());
        for (size_t i = 0; i < encoders.size(); i++) {
            Encoder* e = encoders[i].get();
            Aggregator* agg = encAgg.get();
            encDrainers.emplace_back([e, agg, i] {
                CodecFrame f;
                while (e->GetFrame(f)) {
                    agg->push(std::move(f), i);
                }
                agg->streamFinished();
            });
        }
    }
};

Session::Session() = default;
Session::~Session() = default;

size_t Session::streamCount() const {
    if (!impl_) return 0;
    return impl_->decoders.size() + impl_->encoders.size();
}

size_t Session::open(Kind kind, const std::string& backendName,
                     const std::vector<CodecParams>& perStreamParams) {
    if (!impl_) {
        impl_ = std::make_unique<Impl>();
    }
    if (kind == Kind::Decode) {
        size_t created = 0;
        std::vector<std::unique_ptr<Decoder>> batch;
        for (const auto& p : perStreamParams) {
            auto d = Decoder::Create(backendName);
            if (!d || !d->Initialize(p)) {
                break; // all-or-nothing
            }
            batch.push_back(std::move(d));
            ++created;
        }
        if (created != perStreamParams.size()) {
            return 0; // partial failure: drop the batch
        }
        for (auto& d : batch) {
            impl_->decoders.push_back(std::move(d));
        }
        return created;
    } else {
        size_t created = 0;
        std::vector<std::unique_ptr<Encoder>> batch;
        for (const auto& p : perStreamParams) {
            auto e = Encoder::Create(backendName);
            if (!e || !e->Initialize(p)) {
                break;
            }
            batch.push_back(std::move(e));
            ++created;
        }
        if (created != perStreamParams.size()) {
            return 0;
        }
        for (auto& e : batch) {
            impl_->encoders.push_back(std::move(e));
        }
        return created;
    }
}

int Session::feedDecode(size_t idx, const uint8_t* data, size_t size) {
    if (!impl_ || idx >= impl_->decoders.size()) return -1;
    return impl_->decoders[idx]->FillInput(data, size);
}

bool Session::signalDecodeEOF(size_t idx) {
    if (!impl_ || idx >= impl_->decoders.size()) return false;
    return impl_->decoders[idx]->SignalInputComplete();
}

bool Session::getDecodeFrame(size_t idx, CodecFrame& out) {
    if (!impl_ || idx >= impl_->decoders.size()) return false;
    return impl_->decoders[idx]->GetFrame(out);
}

bool Session::feedEncode(size_t idx, const CodecFrame& in) {
    if (!impl_ || idx >= impl_->encoders.size()) return false;
    return impl_->encoders[idx]->FillFrame(in);
}

bool Session::signalEncodeEOF(size_t idx) {
    if (!impl_ || idx >= impl_->encoders.size()) return false;
    return impl_->encoders[idx]->SignalInputComplete();
}

bool Session::getEncodeFrame(size_t idx, CodecFrame& out) {
    if (!impl_ || idx >= impl_->encoders.size()) return false;
    return impl_->encoders[idx]->GetFrame(out);
}

bool Session::getAnyDecodeFrame(CodecFrame& out, size_t& outStream) {
    if (!impl_ || impl_->decoders.empty()) return false;
    if (!impl_->decAggStarted) {
        impl_->startDecodeAggregator();
    }
    return impl_->decAgg->pop(out, outStream);
}

bool Session::getAnyEncodeFrame(CodecFrame& out, size_t& outStream) {
    if (!impl_ || impl_->encoders.empty()) return false;
    if (!impl_->encAggStarted) {
        impl_->startEncodeAggregator();
    }
    return impl_->encAgg->pop(out, outStream);
}

void Session::close() {
    if (!impl_) return;
    // Resetting the backends triggers their destructors, which signal EOF and
    // join their internal worker threads; that unblocks any drainer threads
    // blocked in GetFrame, after which the Impl destructor joins the drainers.
    impl_->decoders.clear();
    impl_->encoders.clear();
}

} // namespace halcodec
