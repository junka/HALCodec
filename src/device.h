#ifndef SRC_DEVICE_H
#define SRC_DEVICE_H

#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <iostream>

namespace halcodec {

class Device {
protected:
    Device() = default;

public:
    virtual ~Device() = default;

    using Creator = std::function<std::unique_ptr<Device>()>;
    static std::unordered_map<std::string, Device::Creator>& getRegistry() {
        static std::unordered_map<std::string, Device::Creator> registry;
        return registry;
    }

    static std::unique_ptr<Device> Create(const std::string& type) {
        auto& registry = getRegistry();
        auto it = registry.find(type);
        if (it != registry.end()) {
            return it->second();
        } else {
            return nullptr;
        }
    }

    static void RegisterDevice(const std::string& type, Creator creator) {
        getRegistry()[type] = creator;
    }

    static void ShowDevices() {
        auto& registry = getRegistry();
        for (auto it : registry) {
            // std::cout << it.first << std::endl;
        }
    }

    virtual int getNumDevices() { return 0; }
    virtual void createCtx(int idx) { id_ = idx; }
    virtual void destroyCtx() {}
    virtual void showDecoderCapability() {}
    virtual void showEncoderCapability() {}

    int getDeviceIdx() {
        return id_;
    }

    std::string getDeviceName() {
        return name_;
    }
protected:
    int id_;
    std::string name_;
};

} // namespace halcodec

#endif // SRC_DEVICE_H