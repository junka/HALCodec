// channel_factory.cc — 层 2 通道构造器注册表(见 camera/frame_channel.h)。
//
// 同 frame_share.cc 的模型:平台 backend 在 static-init 时注册自己的 kind
// ("unixfd"/"nvscistream"/"machport"),工厂按 kind 构造。default visibility 使其
// 与编进 hal_cam 的 camera/*.cc 共享同一份表。

#include "frame_channel.h"

#include <map>

#include "registry.h"

namespace camera {

namespace {

// 表与锁都由 registry 层持有(见 registry.h):backend .so 与本库共享同一份,
// 而文件局部 static 在 hidden visibility 下每个 .so 各有一份。
struct FactoryTable {
    std::map<std::string, FrameChannelFactoryFn> factories;
};

constexpr const char* kFactoryKey = "halcodec.camera.channel_factory";

} // namespace

__attribute__((visibility("default")))
void RegisterFrameChannelFactory(const std::string& kind, FrameChannelFactoryFn fn) {
    halcodec::with_locked_table<FactoryTable>(
        kFactoryKey, [&](FactoryTable& t) { t.factories[kind] = std::move(fn); });
}

__attribute__((visibility("default")))
bool FrameChannelFactoryRegistered(const std::string& kind) {
    return halcodec::with_locked_table<FactoryTable>(
        kFactoryKey, [&](FactoryTable& t) { return t.factories.count(kind) != 0; });
}

__attribute__((visibility("default")))
std::unique_ptr<FrameChannel> CreateFrameChannel(const std::string& kind,
                                                 const ChannelConfig& cfg) {
    // 工厂函数拷出后在锁外调用:它要构造 backend 对象,可能反过来查/注册表。
    FrameChannelFactoryFn fn = halcodec::with_locked_table<FactoryTable>(
        kFactoryKey, [&](FactoryTable& t) -> FrameChannelFactoryFn {
            auto it = t.factories.find(kind);
            if (it == t.factories.end()) return nullptr;
            return it->second;
        });
    if (!fn) return nullptr;
    return fn(cfg);
}

} // namespace camera
