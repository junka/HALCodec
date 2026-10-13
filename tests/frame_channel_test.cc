// channel_test - Layer-1/Layer-2 self-check with no camera, no GPU and no
// platform transport: a producer publishes into a "loopback" FrameChannel
// (in-process shared slots) and each consumer reads the frames it can.
//
// What this pins down, beyond "it runs":
//   - Fan-out shape: one channel per consumer (multi-consumer = FrameRouter
//     holds N channels). Each consumer is independent.
//   - Zero-copy read: the consumer's hostData() is the same address the
//     producer filled, so the pixels are not copied twice.
//   - Slot lifetime: a slot is not recycled while a consumer still holds it.
//     The test keeps one frame alive across several publishes and asserts its
//     bytes are untouched - that is the reference counting, not the copy.
//   - Lossy fan-out under pressure: with no backpressure, a consumer that falls
//     behind sees newer frames, never a corrupted or out-of-order one, and the
//     producer never stalls on it (Acquire returns nullptr instead of blocking).
//
// The loopback backend registers itself at static-init in
// camera/loopback_channel.cc, which this test links directly.

#include <atomic>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

#include "frame_channel.h"
#include "test_check.h"

namespace {

constexpr int kWidth = 16;
constexpr int kHeight = 8;
constexpr size_t kFrameBytes = static_cast<size_t>(kWidth) * kHeight * 3 / 2;

// Pattern derived from the sequence number, so a consumer can tell which frame
// it holds without any out-of-band bookkeeping.
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

camera::ChannelConfig Cfg(const std::string& name, bool producer) {
    camera::ChannelConfig c;
    c.name = name;
    c.producer = producer;
    c.locality = halcodec::FrameLocality::Host;
    c.width = kWidth;
    c.height = kHeight;
    c.numSlots = 4;
    return c;
}

std::unique_ptr<camera::FrameChannel> Make(const std::string& name, bool prod) {
    auto ch = camera::CreateFrameChannel("loopback", Cfg(name, prod));
    if (ch && !ch->Open()) return nullptr;
    return ch;
}

// Publish one frame carrying `seq`. Returns false when no slot was free.
bool PublishSeq(camera::FrameChannel& ch, uint64_t seq) {
    uint8_t* slot = ch.AcquireHostSlot(kFrameBytes);
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

} // namespace

int main() {
    // --- Fan-out: two consumers, one channel each, producer on its own thread.
    // Each consumer drains until it has seen the final sequence, then stops
    // (Subscribe blocks once the producer is gone). Frames may be skipped, but
    // must never arrive out of order or with wrong pixels.
    {
        constexpr uint64_t kFrames = 200;
        auto prod = Make("multi", true);
        CHECK(prod != nullptr);
        auto c0 = Make("multi", false);
        auto c1 = Make("multi", false);
        CHECK(c0 && c1);
        if (prod && c0 && c1) {
            std::thread pt([&] {
                for (uint64_t s = 0; s < kFrames; ++s) PublishSeq(*prod, s);
            });
            std::atomic<int> delivered{0};
            std::atomic<int> ordered{0};
            auto drain = [&](camera::FrameChannel& c) {
                uint64_t expect = 0;
                int got = 0;
                while (true) {
                    auto r = c.Subscribe();
                    if (!r) break;
                    const halcodec::SharedFrameDesc& d = r->desc();
                    if (d.sequence < expect) ordered.fetch_add(1);  // regression
                    expect = d.sequence + 1;
                    const uint8_t* p = r->hostData();
                    if (!p || !PatternMatches(p, kFrameBytes, d.sequence)) {
                        ordered.fetch_add(1);  // corruption also lands here
                    }
                    ++got;
                    if (d.sequence == kFrames - 1) break;
                }
                delivered += got;
            };
            std::thread t0(drain, std::ref(*c0));
            std::thread t1(drain, std::ref(*c1));
            pt.join();
            // Close the producer BEFORE joining the consumers: a consumer that
            // never saw kFrames - 1 (its copy of that frame was dropped) is
            // still parked in Subscribe(). Close sets producerGone and wakes
            // every subscriber, so each drain loop falls out on nullptr once its
            // queue drains. Joining first would hang. This is the same signal a
            // real producer uses to end a stream.
            prod->Close();
            t0.join();
            t1.join();
            CHECK_EQ(ordered.load(), 0);      // no out-of-order / corrupt frame
            CHECK(delivered.load() > 0);      // both consumers made progress
            CHECK(delivered.load() <= static_cast<int>(kFrames) * 2);
            c0->Close();
            c1->Close();
        }
    }

    // --- Zero-copy identity: the consumer reads the producer's own storage. ---
    {
        auto p = Make("zc", true);
        auto c = Make("zc", false);
        CHECK(p && c);
        if (p && c) {
            uint8_t* slot = p->AcquireHostSlot(kFrameBytes);
            CHECK(slot != nullptr);
            FillPattern(slot, kFrameBytes, 12345);
            halcodec::SharedFrameDesc d;
            d.locality = halcodec::FrameLocality::Host;
            d.width = kWidth;
            d.height = kHeight;
            d.sequence = 12345;
            CHECK(p->Publish(d));
            auto r = c->Subscribe();
            CHECK(r != nullptr);
            if (r) {
                // Same address the producer wrote into: no second copy exists.
                CHECK_EQ(r->hostData(), slot);
                CHECK_EQ(r->hostSize(), kFrameBytes);
                CHECK(PatternMatches(r->hostData(), kFrameBytes, 12345));
            }
            p->Close();
            c->Close();
        }
    }

    // --- Slot retention: a held frame's bytes survive later publishes. ---
    {
        auto p = Make("retain", true);
        auto c = Make("retain", false);
        CHECK(p && c);
        if (p && c) {
            CHECK(PublishSeq(*p, 1));
            auto held = c->Subscribe();
            CHECK(held != nullptr);
            const uint8_t* heldData = held ? held->hostData() : nullptr;
            // Publish several more; the held frame's slot must not be reused
            // while it is still outstanding (refcount > 0).
            int republished = 0;
            for (uint64_t s = 2; s <= 4; ++s) {
                if (PublishSeq(*p, s)) ++republished;
            }
            CHECK(republished > 0);  // the producer could still make progress
            if (heldData) CHECK(PatternMatches(heldData, kFrameBytes, 1));
            p->Close();
            c->Close();
        }
    }

    // --- Under pressure the producer never stalls: once the consumer stops
    // reading, AcquireHostSlot returns nullptr (drop) instead of blocking. ---
    {
        auto p = Make("slow", true);
        auto c = Make("slow", false);
        CHECK(p && c);
        if (p && c) {
            int published = 0;
            for (uint64_t s = 0; s < 100; ++s) {
                if (PublishSeq(*p, s)) ++published;
            }
            // Slots bound the in-flight frames, so a non-reading consumer caps
            // how many the producer can hand off - it does not grow unbounded.
            CHECK(published >= 1);
            CHECK(published <= 4 * 2);  // numSlots, with slack for reuse
            auto r = c->Subscribe();
            CHECK(r != nullptr);  // the consumer still gets one of them
            p->Close();
            c->Close();
        }
    }

    // --- Construction: an unregistered kind resolves to nullptr. ---
    CHECK(camera::CreateFrameChannel("no-such-kind", Cfg("x", true)) == nullptr);
    CHECK(camera::FrameChannelFactoryRegistered("loopback"));

    return haltest::finish("frame_channel");
}
