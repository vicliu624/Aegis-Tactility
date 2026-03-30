#pragma once

#include <Tactility/service/reticulum/Types.h>

#include <functional>
#include <string>

namespace tt::service::reticulum {

class Interface {

public:

    using ReceiveCallback = std::function<void(InboundFrame)>;

    virtual ~Interface() = default;

    virtual std::string getId() const = 0;

    virtual InterfaceKind getKind() const = 0;

    virtual uint32_t getCapabilities() const = 0;

    virtual InterfaceMetrics getMetrics() const = 0;

    virtual void setReceiveCallback(ReceiveCallback callback) = 0;

    virtual bool start() = 0;

    virtual void stop() = 0;

    virtual bool sendFrame(const InterfaceFrame& frame) = 0;
};

} // namespace tt::service::reticulum
