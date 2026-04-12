#pragma once

#include <tactility/freertos/freertos.h>
#include <tactility/hal/Device.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace tt::hal::radio {

struct LoRaConfiguration {
    uint32_t frequency = 491875000U;
    uint32_t bandwidth = 125000U;
    uint8_t txPower = 22U;
    uint8_t spreadingFactor = 8U;
    uint8_t codingRate = 5U;
    uint16_t preambleLength = 8U;
    uint8_t syncWord = 0x12U;
    bool crc = true;
};

struct LoRaMetrics {
    bool available = false;
    int32_t bitrate = 0;
    int16_t rssi = 0;
    int16_t snr = 0;
    size_t hardwareMtu = 255U;
};

class LoRaDevice : public tt::hal::Device {

public:
    using ReceiveCallback = std::function<void(std::vector<uint8_t>)>;

    Type getType() const final { return Type::Radio; }

    virtual bool start(const LoRaConfiguration& configuration, ReceiveCallback receiveCallback) = 0;

    virtual void stop() = 0;

    virtual bool poll(TickType_t timeout) = 0;

    virtual bool send(const std::vector<uint8_t>& payload) = 0;

    virtual LoRaMetrics getMetrics() const = 0;

    virtual std::string getDetail() const = 0;
};

std::vector<std::shared_ptr<LoRaDevice>> findLoRaDevices();

std::shared_ptr<LoRaDevice> findLoRaDevice(std::string_view preferredName = {});

} // namespace tt::hal::radio
