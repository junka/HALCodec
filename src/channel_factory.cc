// channel_factory.cc — 层 2 通道构造器注册表(见 camera/frame_channel.h)。
//
// 同 frame_share.cc 的模型:平台 backend 在 static-init 时注册自己的 kind
// ("unixfd"/"nvscistream"/"machport"),工厂按 kind 构造。default visibility 使其
// 与编进 hal_cam 的 camera/*.cc 共享同一份表。

#include "frame_channel.h"

#include <map>
#include <mutex>

namespace camera {

namespace {
std::map<std::string, FrameChannelFactoryFn>& table() {
    static std::map<std::string, FrameChannelFactoryFn> t;
    return t;
}
std::mutex& mu() {
    static std::mutex m;
    return m;
}
} // namespace

__attribute__((visibility("default")))
void RegisterFrameChannelFactory(const std::string& kind, FrameChannelFactoryFn fn) {
    std::lock_guard<std::mutex> lk(mu());
    table()[kind] = std::move(fn);
}

__attribute__((visibility("default")))
bool FrameChannelFactoryRegistered(const std::string& kind) {
    std::lock_guard<std::mutex> lk(mu());
    return table().count(kind) != 0;
}

__attribute__((visibility("default")))
std::unique_ptr<FrameChannel> CreateFrameChannel(const std::string& kind,
                                                 const ChannelConfig& cfg) {
    FrameChannelFactoryFn fn;
    {
        std::lock_guard<std::mutex> lk(mu());
        auto it = table().find(kind);
        if (it == table().end()) return nullptr;
        fn = it->second;
    }
    return fn(cfg);
}

} // namespace camera
