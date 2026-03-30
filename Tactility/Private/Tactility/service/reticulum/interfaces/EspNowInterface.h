#pragma once

#include <Tactility/Logger.h>
#include <Tactility/service/reticulum/Interface.h>

namespace tt::service::reticulum::interfaces {

class EspNowInterface final : public Interface {

    ReceiveCallback receiveCallback;

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && !defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)
    int receiveSubscription = -1;
#endif

public:

    std::string getId() const override { return "espnow"; }

    InterfaceKind getKind() const override { return InterfaceKind::EspNow; }

    uint32_t getCapabilities() const override {
        return toMask(InterfaceCapability::Broadcast) |
            InterfaceCapability::Transport |
            InterfaceCapability::Link |
            InterfaceCapability::Resource;
    }

    InterfaceMetrics getMetrics() const override;

    void setReceiveCallback(ReceiveCallback callback) override { receiveCallback = std::move(callback); }

    bool start() override;

    void stop() override;

    bool sendFrame(const InterfaceFrame& frame) override;
};

} // namespace tt::service::reticulum::interfaces
