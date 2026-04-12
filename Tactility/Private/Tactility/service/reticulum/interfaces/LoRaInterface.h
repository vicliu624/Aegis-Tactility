#pragma once

#include <Tactility/Mutex.h>
#include <Tactility/Thread.h>
#include <Tactility/service/reticulum/Interface.h>
#include <Tactility/service/reticulum/interfaces/backends/LoRaBackend.h>
#include <Tactility/settings/ReticulumSettings.h>

#include <memory>

namespace tt::service::reticulum::interfaces {

class LoRaInterface final : public Interface {
    ReceiveCallback receiveCallback;
    settings::reticulum::LoRaSettings settings;
    std::unique_ptr<Thread> thread;
    std::shared_ptr<backends::LoRaBackend> backend;
    mutable Mutex mutex;
    bool threadInterrupted = false;

    void handleReceivedPayload(std::vector<uint8_t> payload);
    int32_t runLoop();

public:
    explicit LoRaInterface(settings::reticulum::LoRaSettings settings = settings::reticulum::getDefault()) :
        settings(std::move(settings))
    {}

    ~LoRaInterface() override;

    std::string getId() const override { return "lora"; }

    InterfaceKind getKind() const override { return InterfaceKind::LoRa; }

    uint32_t getCapabilities() const override {
        return toMask(InterfaceCapability::Transport) |
            InterfaceCapability::Link |
            InterfaceCapability::Resource |
            InterfaceCapability::Metrics;
    }

    InterfaceMetrics getMetrics() const override;

    void setReceiveCallback(ReceiveCallback callback) override { receiveCallback = std::move(callback); }

    bool start() override;

    void stop() override;

    bool sendFrame(const InterfaceFrame& frame) override;
};

} // namespace tt::service::reticulum::interfaces
