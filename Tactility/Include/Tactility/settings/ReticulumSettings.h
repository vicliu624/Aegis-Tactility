#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace tt::settings::reticulum {

struct LoRaSettings {
    bool enabled = false;
    uint32_t frequency = 491875000;
    uint32_t bandwidth = 125000;
    uint8_t txPower = 22;
    uint8_t spreadingFactor = 8;
    uint8_t codingRate = 5;
    bool flowControl = false;
};

bool load(LoRaSettings& settings);
LoRaSettings getDefault();
LoRaSettings loadOrGetDefault();
bool save(const LoRaSettings& settings);
bool validate(const LoRaSettings& settings, std::optional<std::string>& error);

} // namespace tt::settings::reticulum
