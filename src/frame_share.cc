// frame_share.cc — 层 1 的注册表与按 locality 分发(见 camera/frame_share.h)。
//
// 与 src/frame.cc 的 download hook 同构:frame.cc 不链接厂商库,靠 backend 注册
// 回调;这里同样只持有 std::function,由产出设备的 camera/*.cc 在 static-init 时
// 安装。default visibility 导出,使 hal_cam 里编入的 camera 源文件与 halcodec_core
// 共享同一份注册表(否则每个 .so 各自一份 static,注册看不到)。

#include "frame_share.h"

#include <array>

#include "registry.h"

namespace halcodec {

namespace {

struct Entry {
    FrameExportFn exportFn;
    FrameImportFn importFn;
    FrameTransportCloseFn closeFn;
};

// 每 locality 一格。locality 是小枚举,固定数组足够,免去 map。
struct ShareTable {
    std::array<Entry, 8> entries;
};

// 表与锁都由 registry 层持有(见 registry.h):backend .so 与本库共享同一份,
// 而文件局部 static 在 hidden visibility 下每个 .so 各有一份。
constexpr const char* kShareKey = "halcodec.frame.share";

size_t index(FrameLocality l) {
    return static_cast<size_t>(l);
}

} // namespace

__attribute__((visibility("default")))
void RegisterFrameShare(FrameLocality locality, FrameExportFn e, FrameImportFn i) {
    size_t idx = index(locality);
    with_locked_table<ShareTable>(kShareKey, [&](ShareTable& t) {
        if (idx >= t.entries.size()) return;
        t.entries[idx].exportFn = std::move(e);
        t.entries[idx].importFn = std::move(i);
    });
}

// 与 RegisterFrameShare 分开:多数 backend 无需在端点关闭时做清理,把 close 做成
// 独立注册可避免为它们强塞一个空函数(maintainer 只需在真有缓存时安装)。
__attribute__((visibility("default")))
void RegisterFrameTransportClose(FrameLocality locality, FrameTransportCloseFn f) {
    size_t idx = index(locality);
    with_locked_table<ShareTable>(kShareKey, [&](ShareTable& t) {
        if (idx >= t.entries.size()) return;
        t.entries[idx].closeFn = std::move(f);
    });
}

__attribute__((visibility("default")))
void CloseSharedTransport(FrameLocality locality, uint64_t transport) {
    size_t idx = index(locality);
    FrameTransportCloseFn fn = with_locked_table<ShareTable>(
        kShareKey, [&](ShareTable& t) -> FrameTransportCloseFn {
            if (idx >= t.entries.size()) return nullptr;
            return t.entries[idx].closeFn;
        });
    if (fn) fn(transport);  // 未注册 = 该 backend 无传输态需要释放
}

__attribute__((visibility("default")))
bool FrameShareRegistered(FrameLocality locality) {
    size_t idx = index(locality);
    return with_locked_table<ShareTable>(kShareKey, [&](ShareTable& t) {
        if (idx >= t.entries.size()) return false;
        return static_cast<bool>(t.entries[idx].exportFn) &&
               static_cast<bool>(t.entries[idx].importFn);
    });
}

__attribute__((visibility("default")))
bool ExportSharedFrame(const CodecFrame& frame, SharedFrameDesc& desc) {
    size_t idx = index(frame.locality);
    FrameExportFn fn = with_locked_table<ShareTable>(
        kShareKey, [&](ShareTable& t) -> FrameExportFn {
            if (idx >= t.entries.size()) return nullptr;
            return t.entries[idx].exportFn;
        });
    return fn ? fn(frame, desc) : false;
}

__attribute__((visibility("default")))
bool ImportSharedFrame(const SharedFrameDesc& desc, CodecFrame& frame) {
    size_t idx = index(desc.locality);
    FrameImportFn fn = with_locked_table<ShareTable>(
        kShareKey, [&](ShareTable& t) -> FrameImportFn {
            if (idx >= t.entries.size()) return nullptr;
            return t.entries[idx].importFn;
        });
    return fn ? fn(desc, frame) : false;
}

} // namespace halcodec
