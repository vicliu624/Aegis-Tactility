#pragma once

#include <Tactility/service/reticulum/Interface.h>

namespace tt::service::reticulum::interfaces {

class LoRaInterface final : public Interface {

    ReceiveCallback receiveCallback;

public:

    std::string getId() const override { return "lora"; }

    InterfaceKind getKind() const override { return InterfaceKind::LoRa; }

    uint32_t getCapabilities() const override {
        return toMask(InterfaceCapability::Transport) |
            InterfaceCapability::Link |
            InterfaceCapability::Resource |
            InterfaceCapability::Metrics;
    }

    InterfaceMetrics getMetrics() const override { return InterfaceMetrics {}; }

    void setReceiveCallback(ReceiveCallback callback) override { receiveCallback = std::move(callback); }

    bool start() override;

    void stop() override {}

    bool sendFrame(const InterfaceFrame& frame) override;
};

} // namespace tt::service::reticulum::interfaces
