#ifndef SRC_DEVICE_H
#define SRC_DEVICE_H

#include <cstdio>
#include <memory>
#include <string>

#include "registry.h"

namespace halcodec {

class Device {
protected:
    Device() = default;

public:
    virtual ~Device() = default;

    using Creator = Registry<Device>::Creator;

    // Factory: instantiate a registered device by its vendor name
    // (e.g. "nvidia", "vtbox").
    static std::unique_ptr<Device> Create(const std::string& type) {
        return Registry<Device>::Create(type);
    }

    static bool Register(const std::string& type, Creator creator) {
        return Registry<Device>::Register(type, std::move(creator));
    }

    static void ShowDevices() {
        for (const auto& name : Registry<Device>::Names()) {
            std::printf("%s\n", name.c_str());
        }
    }

    virtual int getNumDevices() const { return 0; }
    virtual void createCtx(int idx) { id_ = idx; }
    virtual void destroyCtx() {}
    virtual void showDecoderCapability() const {}
    virtual void showEncoderCapability() const {}

    int getDeviceIdx() const { return id_; }

    std::string getDeviceName() const { return name_; }

protected:
    int id_ = -1;
    std::string name_;
};

} // namespace halcodec

#endif // SRC_DEVICE_H