// loopback_channel.cc — 进程内(单进程多消费者)通道,层 2 的一个 backend。
//
// 用途:框架自检 + 不跨进程的多路读取——把一路采集的同一块 buffer 分发给多个
// SharedFrameSource,验证 FrameRouter / SharedFrameSource / FrameChannel 三个抽象
// 的形状。后续 unixfd / nvscistream backend 只是把"共享堆槽"换成真实传输。
//
// 实现:按名字共享一个 LoopbackRegion(若干 Host 槽 + 每槽引用计数)。producer 用
// AcquireHostSlot 取空闲槽填像素,Publish 把(槽号 + 元数据)投进每个消费者队列并把
// 引用计数 += 订阅者数;consumer 的 ReceivedFrame 析构时引用计数 -= 1,归零即可复用。
// 不跨进程,故像素就是内存里的同一份(真零拷贝)。

#include "frame_channel.h"

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace camera {

namespace {

// 一块可写 Host 槽。size_ 按首帧几何确定(几何变化时重分配)。
struct Slot {
    std::vector<uint8_t> data;
    int refcount = 0;   // 仍有几个消费者/取帧持有者未释放
    uint64_t seq = 0;   // 最近一次写入的帧序号(诊断)
};

// 一个端点的共享区域:槽数组 + 每消费者的待取队列。
struct LoopbackRegion {
    std::mutex mu;
    std::vector<Slot> slots;
    size_t slotSize = 0;

    struct Consumer {
        std::mutex mu;
        std::condition_variable cv;
        std::deque<std::pair<int, halcodec::SharedFrameDesc>> q;
        bool closed = false;
        // 生产者已关闭:流结束。Subscribe 在队列排空后返回 nullptr,不会永久阻塞。
        bool producerGone = false;
    };
    std::map<LoopbackRegion::Consumer*,
             std::shared_ptr<LoopbackRegion::Consumer>> consumers;
    bool producerAlive = true;
};
std::mutex& regionMapMu() {
    static std::mutex m;
    return m;
}
std::map<std::string, std::shared_ptr<LoopbackRegion>>& regionMap() {
    static std::map<std::string, std::shared_ptr<LoopbackRegion>> m;
    return m;
}
std::shared_ptr<LoopbackRegion> getRegion(const std::string& name, int numSlots) {
    std::lock_guard<std::mutex> lk(regionMapMu());
    auto& r = regionMap()[name];
    if (!r) {
        r = std::make_shared<LoopbackRegion>();
        r->slots.resize(std::max(1, numSlots));
    }
    return r;
}

class LoopbackChannel;

// 消费者取到的一帧:指向共享槽的只读视图;析构时把槽的引用计数减一。
class LoopbackReceived : public ReceivedFrame {
public:
    LoopbackReceived(std::shared_ptr<LoopbackRegion> r, int slot,
                     halcodec::SharedFrameDesc desc)
        : region_(std::move(r)), slot_(slot), desc_(std::move(desc)) {}
    ~LoopbackReceived() override {
        std::lock_guard<std::mutex> lk(region_->mu);
        if (slot_ >= 0 && slot_ < static_cast<int>(region_->slots.size())) {
            if (region_->slots[slot_].refcount > 0) region_->slots[slot_].refcount--;
        }
    }
    const halcodec::SharedFrameDesc& desc() const override { return desc_; }
    uint8_t* hostData() override { return region_->slots[slot_].data.data(); }
    size_t hostSize() override { return region_->slots[slot_].data.size(); }

private:
    std::shared_ptr<LoopbackRegion> region_;
    int slot_;
    halcodec::SharedFrameDesc desc_;
};

class LoopbackChannel : public FrameChannel {
public:
    LoopbackChannel(const ChannelConfig& cfg)
        : name_(cfg.name), producer_(cfg.producer), numSlots_(cfg.numSlots) {}

    bool Open() override {
        region_ = getRegion(name_, numSlots_);
        if (!producer_) {
            consumer_ = std::make_shared<LoopbackRegion::Consumer>();
            std::lock_guard<std::mutex> lk(region_->mu);
            region_->consumers[consumer_.get()] = consumer_;
        }
        return true;
    }

    uint8_t* AcquireHostSlot(size_t bytes) override {
        if (!producer_ || !region_) return nullptr;
        std::lock_guard<std::mutex> lk(region_->mu);
        if (region_->slotSize != bytes) {
            // 几何变化(或首帧):所有槽重分配。仅在全部空闲时安全。
            bool anyBusy = false;
            for (auto& s : region_->slots) if (s.refcount > 0) anyBusy = true;
            if (anyBusy) return nullptr;
            for (auto& s : region_->slots) s.data.assign(bytes, 0);
            region_->slotSize = bytes;
        }
        for (size_t i = 0; i < region_->slots.size(); ++i) {
            if (region_->slots[i].refcount == 0) {
                acquiredSlot_ = static_cast<int>(i);
                // 预留:填充期间把计数置 -1,避免同一生产者连续 Acquire 到同一槽,
                // 也避免消费者在 Publish 前误取。Publish 时改写为订阅者数。
                region_->slots[i].refcount = -1;
                return region_->slots[i].data.data();
            }
        }
        return nullptr;  // 全部槽被消费者占用 → router 丢弃本帧
    }

    bool Publish(const halcodec::SharedFrameDesc& desc) override {
        if (!producer_ || !region_ || acquiredSlot_ < 0) return false;
        std::vector<std::shared_ptr<LoopbackRegion::Consumer>> subs;
        {
            std::lock_guard<std::mutex> lk(region_->mu);
            // 本帧引用计数 = 当前订阅者数(每个消费者各持一份;无消费者则立即空闲)。
            region_->slots[acquiredSlot_].refcount =
                static_cast<int>(region_->consumers.size());
            region_->slots[acquiredSlot_].seq = desc.sequence;
            for (auto& kv : region_->consumers) subs.push_back(kv.second);
        }
        for (auto& c : subs) {
            std::lock_guard<std::mutex> lk(c->mu);
            if (c->q.size() >= static_cast<size_t>(std::max(1, numSlots_))) {
                // drop-oldest:归还被丢弃帧所占槽的引用(该帧从未交给消费者,
                // 故不会有 ReceivedFrame 去减它),否则槽永久泄漏、生产者最终停摆。
                DropRefLocked(c->q.front().first);
                c->q.pop_front();
            }
            c->q.push_back({acquiredSlot_, desc});
            c->cv.notify_one();
        }
        acquiredSlot_ = -1;
        return true;
    }

    std::unique_ptr<ReceivedFrame> Subscribe() override {
        if (consumer_ == nullptr) return nullptr;
        std::unique_lock<std::mutex> lk(consumer_->mu);
        consumer_->cv.wait(lk, [&] {
            return !consumer_->q.empty() || consumer_->closed ||
                   consumer_->producerGone;
        });
        if (consumer_->q.empty()) return nullptr;  // 排空或关闭:流结束
        auto [slot, desc] = std::move(consumer_->q.front());
        consumer_->q.pop_front();
        return std::make_unique<LoopbackReceived>(region_, slot, desc);
    }

    void Close() override {
        if (producer_ && region_) {
            std::vector<std::shared_ptr<LoopbackRegion::Consumer>> subs;
            {
                std::lock_guard<std::mutex> lk(region_->mu);
                region_->producerAlive = false;
                for (auto& kv : region_->consumers) subs.push_back(kv.second);
            }
            // 逐个置位并唤醒,但不在持 region_->mu 时取 consumer->mu:否则与
            // Publish 丢帧路径(持 consumer->mu 再取 region_->mu)形成 ABBA 死锁。
            for (auto& c : subs) {
                std::lock_guard<std::mutex> clk(c->mu);
                c->producerGone = true;
            }
            for (auto& c : subs) c->cv.notify_all();
        }
        if (consumer_ && region_) {
            {
                // 丢弃未取帧的引用,否则这些槽永不回收(见 Publish 的 drop 分支)。
                std::lock_guard<std::mutex> clk(consumer_->mu);
                for (auto& e : consumer_->q) DropRefLocked(e.first);
                consumer_->q.clear();
                consumer_->closed = true;
            }
            std::lock_guard<std::mutex> lk(region_->mu);
            region_->consumers.erase(consumer_.get());
            consumer_->cv.notify_all();
        }
    }

    halcodec::FrameLocality locality() const override {
        return halcodec::FrameLocality::Host;
    }
    const std::string& name() const override { return name_; }

private:
    // 归还一个从未交给消费者的槽引用(队列丢弃 / 关闭时)。调用方不得持 region_
    // 锁;消费端只持 consumer_->mu,故 region_ 锁的嵌套顺序恒为 consumer→region。
    void DropRefLocked(int slot) {
        std::lock_guard<std::mutex> lk(region_->mu);
        if (slot >= 0 && slot < static_cast<int>(region_->slots.size()) &&
            region_->slots[slot].refcount > 0) {
            region_->slots[slot].refcount--;
        }
    }

    std::string name_;
    bool producer_;
    int numSlots_;
    std::shared_ptr<LoopbackRegion> region_;
    std::shared_ptr<LoopbackRegion::Consumer> consumer_;
    int acquiredSlot_ = -1;
};

} // namespace

void RegisterLoopbackChannel() {
    RegisterFrameChannelFactory(
        "loopback", [](const ChannelConfig& cfg) -> std::unique_ptr<FrameChannel> {
            return std::make_unique<LoopbackChannel>(cfg);
        });
}

namespace {
// 静态注册:本文件编进 hal_cam 即让 kind "loopback" 可用,进程启动时完成
// (注册进 halcodec_core 的共享表,见 src/channel_factory.cc)。
struct LoopbackRegistrar {
    LoopbackRegistrar() { RegisterLoopbackChannel(); }
} g_loopbackRegistrar;
} // namespace

} // namespace camera
