#include <Tactility/settings/ReticulumSettings.h>

#include <Tactility/Logger.h>
#include <Tactility/file/PropertiesFile.h>

#include <charconv>
#include <map>
#include <string_view>

namespace tt::settings::reticulum {

static const auto LOGGER = Logger("ReticulumSettings");
constexpr auto* SETTINGS_FILE = "/data/settings/reticulum.properties";

constexpr auto* KEY_ENABLED = "loraEnabled";
constexpr auto* KEY_FREQUENCY = "loraFrequency";
constexpr auto* KEY_BANDWIDTH = "loraBandwidth";
constexpr auto* KEY_TX_POWER = "loraTxPower";
constexpr auto* KEY_SPREADING_FACTOR = "loraSpreadingFactor";
constexpr auto* KEY_CODING_RATE = "loraCodingRate";
constexpr auto* KEY_FLOW_CONTROL = "loraFlowControl";

static bool parseUint32(const std::string& value, uint32_t& out) {
    auto* begin = value.data();
    auto* end = value.data() + value.size();
    return std::from_chars(begin, end, out).ec == std::errc {};
}

static bool parseUint8(const std::string& value, uint8_t& out) {
    uint32_t parsed = 0;
    if (!parseUint32(value, parsed) || parsed > 0xFF) {
        return false;
    }

    out = static_cast<uint8_t>(parsed);
    return true;
}

LoRaSettings getDefault() {
    LoRaSettings settings {};
#ifdef CONFIG_TT_DEVICE_ID
    if (std::string_view(CONFIG_TT_DEVICE_ID) == "lilygo-tdeck") {
        settings.enabled = true;
    }
#endif
    return settings;
}

bool load(LoRaSettings& settings) {
    std::map<std::string, std::string> map;
    if (!file::loadPropertiesFile(SETTINGS_FILE, map)) {
        return false;
    }

    settings = getDefault();

    if (const auto it = map.find(KEY_ENABLED); it != map.end()) {
        settings.enabled = it->second == "1" || it->second == "true";
    }

    if (const auto it = map.find(KEY_FREQUENCY); it != map.end()) {
        uint32_t parsed = 0;
        if (parseUint32(it->second, parsed) && parsed > 0) {
            settings.frequency = parsed;
        }
    }

    if (const auto it = map.find(KEY_BANDWIDTH); it != map.end()) {
        uint32_t parsed = 0;
        if (parseUint32(it->second, parsed) && parsed > 0) {
            settings.bandwidth = parsed;
        }
    }

    if (const auto it = map.find(KEY_TX_POWER); it != map.end()) {
        uint8_t parsed = 0;
        if (parseUint8(it->second, parsed)) {
            settings.txPower = parsed;
        }
    }

    if (const auto it = map.find(KEY_SPREADING_FACTOR); it != map.end()) {
        uint8_t parsed = 0;
        if (parseUint8(it->second, parsed)) {
            settings.spreadingFactor = parsed;
        }
    }

    if (const auto it = map.find(KEY_CODING_RATE); it != map.end()) {
        uint8_t parsed = 0;
        if (parseUint8(it->second, parsed)) {
            settings.codingRate = parsed;
        }
    }

    if (const auto it = map.find(KEY_FLOW_CONTROL); it != map.end()) {
        settings.flowControl = it->second == "1" || it->second == "true";
    }

    return true;
}

LoRaSettings loadOrGetDefault() {
    LoRaSettings settings;
    if (!load(settings)) {
        settings = getDefault();
        if (!save(settings)) {
            LOGGER.warn("Failed to save default Reticulum LoRa settings");
        }
    }

    return settings;
}

bool save(const LoRaSettings& settings) {
    std::map<std::string, std::string> map;
    map[KEY_ENABLED] = settings.enabled ? "1" : "0";
    map[KEY_FREQUENCY] = std::to_string(settings.frequency);
    map[KEY_BANDWIDTH] = std::to_string(settings.bandwidth);
    map[KEY_TX_POWER] = std::to_string(settings.txPower);
    map[KEY_SPREADING_FACTOR] = std::to_string(settings.spreadingFactor);
    map[KEY_CODING_RATE] = std::to_string(settings.codingRate);
    map[KEY_FLOW_CONTROL] = settings.flowControl ? "1" : "0";

    return file::savePropertiesFile(SETTINGS_FILE, map);
}

bool validate(const LoRaSettings& settings, std::optional<std::string>& error) {
    error.reset();

    if (!settings.enabled) {
        return true;
    }

    if (settings.frequency < 137000000U || settings.frequency > 3000000000U) {
        error = "Frequency must be between 137000000 and 3000000000 Hz";
        return false;
    }

    if (settings.bandwidth < 7800U || settings.bandwidth > 1625000U) {
        error = "Bandwidth must be between 7800 and 1625000 Hz";
        return false;
    }

    if (settings.txPower > 37U) {
        error = "TX power must be between 0 and 37 dBm";
        return false;
    }

    if (settings.spreadingFactor < 5U || settings.spreadingFactor > 12U) {
        error = "Spreading factor must be between 5 and 12";
        return false;
    }

    if (settings.codingRate < 5U || settings.codingRate > 8U) {
        error = "Coding rate must be between 5 and 8";
        return false;
    }

    return true;
}

} // namespace tt::settings::reticulum
