// unixfd_channel_test - the Linux cross-process backend, exercised for real:
// a POSIX shared ring in a memfd whose fd crosses an AF_UNIX SOCK_SEQPACKET
// socket via SCM_RIGHTS, so what this test proves about fd passing and shared
// memory is not simulated.
//
// What it pins down:
//   - Handshake: a consumer that connects before the producer is up still ends
//     up attached (the ring's geometry comes from the ChannelConfig the
//     producer was built with, so it is laid out before any consumer arrives).
//   - Zero-copy read: the consumer reads pixels out of its own mapping of the
//     same physical pages - the bytes are the producer's pattern with no copy
//     in the channel.
//   - Fan-out: two consumers on one channel each get the frame (one channel per
//     consumer is the intended shape; this also checks the index assignment).
//   - A consumer that goes away (process death / close) does not lock a slot
//     forever: the producer reclaims and keeps publishing.
//   - A genuinely separate process: the consumer runs in a fork()ed child and
//     reports what it saw over a pipe.
//
// Linux-only, like the backend it links.

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "frame_channel.h"
#include "test_check.h"

namespace {

constexpr int kWidth = 32;
constexpr int kHeight = 16;
constexpr size_t kFrameBytes = static_cast<size_t>(kWidth) * kHeight * 3 / 2;

inline uint8_t Pat(uint64_t seq, size_t i) {
    return static_cast<uint8_t>((seq * 31 + i * 7) & 0xff);
}
void FillPattern(uint8_t* p, size_t n, uint64_t seq) {
    for (size_t i = 0; i < n; ++i) p[i] = Pat(seq, i);
}
bool PatternMatches(const uint8_t* p, size_t n, uint64_t seq) {
    for (size_t i = 0; i < n; ++i) {
        if (p[i] != Pat(seq, i)) return false;
    }
    return true;
}

camera::ChannelConfig Cfg(const std::string& name, bool producer, int slots = 4) {
    camera::ChannelConfig c;
    c.name = name;
    c.producer = producer;
    c.locality = halcodec::FrameLocality::Host;
    c.width = kWidth;
    c.height = kHeight;
    c.numSlots = slots;
    return c;
}

std::unique_ptr<camera::FrameChannel> Make(const std::string& name, bool prod,
                                           int slots = 4) {
    auto ch = camera::CreateFrameChannel("unixfd", Cfg(name, prod, slots));
    if (!ch || !ch->Open()) return nullptr;
    return ch;
}

// The channel is deliberately lossy: AcquireHostSlot returns nullptr while all
// slots are held by consumers, and a publisher that ignores that drops frames.
// These correctness cases want the bytes to arrive, so wait for a free slot
// (the backpressure a real producer applies) instead of racing the consumers.
bool PublishSeq(camera::FrameChannel& ch, uint64_t seq) {
    uint8_t* slot = nullptr;
    for (int attempt = 0; attempt < 5000 && !slot; ++attempt) {
        slot = ch.AcquireHostSlot(kFrameBytes);
        if (!slot) ::usleep(100);  // 5s ceiling: a stuck consumer still fails
    }
    if (!slot) return false;
    FillPattern(slot, kFrameBytes, seq);
    halcodec::SharedFrameDesc d;
    d.locality = halcodec::FrameLocality::Host;
    d.format = halcodec::PixelFormat::NV12;
    d.width = kWidth;
    d.height = kHeight;
    d.sequence = seq;
    return ch.Publish(d);
}

// Not a function-local static: two call sites with different tags must get
// different names, and the child inherits pid so its name matches the parent's.
std::string ChanName(const char* tag) {
    return std::string(tag) + "." + std::to_string(::getpid());
}

// Consumer side used by the fork case: read `want` frames, require the bytes to
// be one of the published patterns and never out of order, then report over the
// pipe. Returns an exit code.
int ChildConsumer(const std::string& name, int reportFd, uint64_t want) {
    auto c = Make(name, false);
    if (!c) return 1;
    // 通知父进程"已挂上":环槽是预分配的,但帧只发给发布那一刻已连上的消费者
    // (mask==0 时该帧等于丢弃),所以必须先挂上再让父进程开播,否则 50 帧全丢。
    uint8_t ready = 4;
    ssize_t rw = ::write(reportFd, &ready, 1);
    (void)rw;
    uint64_t expect = 0;
    uint64_t got = 0;
    bool bad = false;
    while (got < want) {
        auto r = c->Subscribe();
        if (!r) break;
        const halcodec::SharedFrameDesc& d = r->desc();
        if (d.sequence < expect) bad = true;      // out of order
        if (d.sequence >= want) continue;         // publish overshoot
        expect = d.sequence + 1;
        if (!r->hostData() || !PatternMatches(r->hostData(), kFrameBytes, d.sequence)) {
            bad = true;
        }
        ++got;
    }
    c->Close();
    uint8_t out = bad ? 2 : static_cast<uint8_t>(got > 0 ? 0 : 3);
    ssize_t w = ::write(reportFd, &out, 1);
    (void)w;
    return bad ? 2 : 0;
}

} // namespace

int main() {
    // --- Same-process: producer plus two consumers on one endpoint. ---
    {
        auto prod = Make(ChanName("fan"), true);
        CHECK(prod != nullptr);
        auto c0 = Make(ChanName("fan"), false);
        auto c1 = Make(ChanName("fan"), false);
        CHECK(c0 != nullptr);
        CHECK(c1 != nullptr);
        if (prod && c0 && c1) {
            constexpr uint64_t kFrames = 100;
            std::thread pt([&] {
                for (uint64_t s = 0; s < kFrames; ++s) PublishSeq(*prod, s);
            });
            std::atomic<int> ordered{0};
            std::atomic<int> delivered{0};
            auto drain = [&](camera::FrameChannel& c) {
                uint64_t expect = 0;
                int got = 0;
                while (true) {
                    auto r = c.Subscribe();
                    if (!r) break;
                    const halcodec::SharedFrameDesc& d = r->desc();
                    if (d.sequence < expect) ordered.fetch_add(1);
                    expect = d.sequence + 1;
                    if (!r->hostData() ||
                        !PatternMatches(r->hostData(), kFrameBytes, d.sequence)) {
                        ordered.fetch_add(1);
                    }
                    ++got;
                    if (d.sequence == kFrames - 1) break;
                }
                delivered += got;
            };
            std::thread t0(drain, std::ref(*c0));
            std::thread t1(drain, std::ref(*c1));
            pt.join();
            prod->Close();   // producer-gone wakes the consumers
            t0.join();
            t1.join();
            CHECK_EQ(ordered.load(), 0);
            CHECK_EQ(delivered.load(), static_cast<int>(kFrames) * 2);
            c0->Close();
            c1->Close();
        }
    }

    // --- Slot reclamation: an abandoned consumer must not lock a slot. ---
    {
        auto prod = Make(ChanName("abandon"), true, 2);
        CHECK(prod != nullptr);
        if (prod) {
            auto c = Make(ChanName("abandon"), false);
            CHECK(c != nullptr);
            CHECK(PublishSeq(*prod, 1));
            CHECK(c->Subscribe() != nullptr);
            c->Close();  // consumer gone, may still hold a slot bit
            // The producer must still be able to publish many frames: either the
            // slot was idle already or the accept thread reclaimed the held bit.
            int published = 0;
            for (uint64_t s = 2; s < 40; ++s) {
                if (PublishSeq(*prod, s)) ++published;
            }
            CHECK(published >= 30);  // not wedged on a dead consumer's slot
            prod->Close();
        }
    }

    // --- Real separate process: consumer in a forked child. ---
    {
        const std::string name = ChanName("fork");
        int pipefd[2];
        CHECK(::pipe(pipefd) == 0);
        pid_t pid = ::fork();
        CHECK(pid >= 0);
        if (pid == 0) {
            ::close(pipefd[0]);
            _exit(ChildConsumer(name, pipefd[1], 50));
        }
        ::close(pipefd[1]);
        auto prod = Make(name, true);
        CHECK(prod != nullptr);
        // 等子进程握手完成(收到 ready 字节)再发布:否则它在 mask==0 期间发的帧
        // 全被丢掉。
        uint8_t ready = 0;
        ssize_t rn = ::read(pipefd[0], &ready, 1);
        CHECK_EQ(static_cast<int>(rn), 1);
        if (prod) {
            for (uint64_t s = 0; s < 50; ++s) PublishSeq(*prod, s);
        }
        uint8_t childOut = 0xff;
        ssize_t n = ::read(pipefd[0], &childOut, 1);
        ::close(pipefd[0]);
        CHECK_EQ(static_cast<int>(n), 1);
        CHECK_EQ(static_cast<int>(childOut), 0);  // child saw frames, none corrupt
        if (prod) prod->Close();
        int status = 0;
        ::waitpid(pid, &status, 0);
        CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }

    // --- Construction: registered on Linux, unknown kinds resolve to nullptr. ---
    CHECK(camera::FrameChannelFactoryRegistered("unixfd"));
    CHECK(camera::CreateFrameChannel("no-such-kind", Cfg("x", true)) == nullptr);

    return haltest::finish("unixfd_channel");
}
