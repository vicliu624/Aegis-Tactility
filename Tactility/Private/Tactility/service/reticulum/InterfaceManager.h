#pragma once

#include <Tactility/RecursiveMutex.h>
#include <Tactility/service/reticulum/Interface.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace tt::service::reticulum {

class InterfaceManager final {

    struct Record {
        std::shared_ptr<Interface> interfaceInstance;
        InterfaceDescriptor descriptor;
    };

    mutable RecursiveMutex mutex;
    std::unordered_map<std::string, Record> records {};

public:

    using ReceiveCallback = std::function<void(InboundFrame)>;

    bool addInterface(const std::shared_ptr<Interface>& interfaceInstance, ReceiveCallback receiveCallback);

    bool removeInterface(const std::string& interfaceId);

    void stopAll();

    std::vector<InterfaceDescriptor> getInterfaces() const;

    bool sendFrame(const std::string& interfaceId, const InterfaceFrame& frame);
};

} // namespace tt::service::reticulum
