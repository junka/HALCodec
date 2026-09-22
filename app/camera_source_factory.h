#ifndef APP_CAMERA_SOURCE_FACTORY_H
#define APP_CAMERA_SOURCE_FACTORY_H

#include <memory>
#include <vector>

#include "camera_source.h"
#include "parse_cli.h"

namespace camera {

// 相机源工厂:解析 CLI 中的源专属参数,构造并打开(Open + Prepare 全部完成)
// 一路或多路 CameraSource。实现分别在 camera_source_{sipl,v4l2,av}.cc,
// 由 CMake 按平台编译。返回 false 表示创建失败(已打印原因)。
bool CreateSIPL(const CommandLineParser& cli,
                std::vector<std::unique_ptr<halcodec::CameraSource>>& out);
bool CreateV4L2(const CommandLineParser& cli,
                std::vector<std::unique_ptr<halcodec::CameraSource>>& out);
bool CreateAVF(const CommandLineParser& cli,
               std::vector<std::unique_ptr<halcodec::CameraSource>>& out);

} // namespace camera

#endif // APP_CAMERA_SOURCE_FACTORY_H