#pragma once

#include <Tactility/service/reticulum/Types.h>
#include <Tactility/settings/ReticulumSettings.h>

#include <tactility/freertos/freertos.h>

#include <functional>
#include <string>
#include <vector>

namespace tt::service::reticulum::interfaces::backends {

class LoRaBackend {

public:
    using ReceiveCallback = std::function<void(std::vector<uint8_t>)>;

    virtual ~LoRaBackend() = default;

    virtual std::string getName() const = 0;

    virtual std::string getDetail() const = 0;

    virtual bool start(const settings::reticulum::LoRaSettings& settings, ReceiveCallback receiveCallback) = 0;

    virtual void stop() = 0;

    virtual bool poll(TickType_t timeout) = 0;

    virtual bool send(const std::vector<uint8_t>& payload) = 0;

    virtual InterfaceMetrics getMetrics() const = 0;
};

} // namespace tt::service::reticulum::interfaces::backends
