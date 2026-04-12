#pragma once

#include <Tactility/hal/radio/LoRaDevice.h>
#include <Tactility/service/reticulum/interfaces/backends/LoRaBackend.h>

#include <memory>

namespace tt::service::reticulum::interfaces::backends {

class NativeLoRaBackend final : public LoRaBackend {
    std::shared_ptr<tt::hal::radio::LoRaDevice> device;

    static InterfaceMetrics toInterfaceMetrics(const tt::hal::radio::LoRaMetrics& metrics);

public:
    explicit NativeLoRaBackend(std::shared_ptr<tt::hal::radio::LoRaDevice> device) :
        device(std::move(device))
    {}

    std::string getName() const override;
    std::string getDetail() const override;
    bool start(const settings::reticulum::LoRaSettings& settings, ReceiveCallback receiveCallback) override;
    void stop() override;
    bool poll(TickType_t timeout) override;
    bool send(const std::vector<uint8_t>& payload) override;
    InterfaceMetrics getMetrics() const override;
};

} // namespace tt::service::reticulum::interfaces::backends
