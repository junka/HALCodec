// frame_share.cc — 层 1 的注册表与按 locality 分发(见 camera/frame_share.h)。
//
// 与 src/frame.cc 的 download hook 同构:frame.cc 不链接厂商库,靠 backend 注册
// 回调;这里同样只持有 std::function,由产出设备的 camera/*.cc 在 static-init 时
// 安装。default visibility 导出,使 hal_cam 里编入的 camera 源文件与 halcodec_core
// 共享同一份注册表(否则每个 .so 各自一份 static,注册看不到)。

#include "frame_share.h"

#include <array>
#include <mutex>

namespace halcodec {

namespace {

struct Entry {
    FrameExportFn exportFn;
    FrameImportFn importFn;
};

// 每 locality 一格。locality 是小枚举,固定数组足够,免去 map。
std::array<Entry, 8>& table() {
    static std::array<Entry, 8> t;
    return t;
}
std::mutex& mu() {
    static std::mutex m;
    return m;
}

size_t index(FrameLocality l) {
    return static_cast<size_t>(l);
}

} // namespace

__attribute__((visibility("default")))
void RegisterFrameShare(FrameLocality locality, FrameExportFn e, FrameImportFn i) {
    std::lock_guard<std::mutex> lk(mu());
    size_t idx = index(locality);
    if (idx >= table().size()) return;
    table()[idx].exportFn = std::move(e);
    table()[idx].importFn = std::move(i);
}

__attribute__((visibility("default")))
bool FrameShareRegistered(FrameLocality locality) {
    std::lock_guard<std::mutex> lk(mu());
    size_t idx = index(locality);
    if (idx >= table().size()) return false;
    return static_cast<bool>(table()[idx].exportFn) &&
           static_cast<bool>(table()[idx].importFn);
}

__attribute__((visibility("default")))
bool ExportSharedFrame(const CodecFrame& frame, SharedFrameDesc& desc) {
    FrameExportFn fn;
    {
        std::lock_guard<std::mutex> lk(mu());
        size_t idx = index(frame.locality);
        if (idx >= table().size()) return false;
        fn = table()[idx].exportFn;
    }
    return fn ? fn(frame, desc) : false;
}

__attribute__((visibility("default")))
bool ImportSharedFrame(const SharedFrameDesc& desc, CodecFrame& frame) {
    FrameImportFn fn;
    {
        std::lock_guard<std::mutex> lk(mu());
        size_t idx = index(desc.locality);
        if (idx >= table().size()) return false;
        fn = table()[idx].importFn;
    }
    return fn ? fn(desc, frame) : false;
}

} // namespace halcodec
