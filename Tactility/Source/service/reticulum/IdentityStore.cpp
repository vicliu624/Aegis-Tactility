#include <Tactility/service/reticulum/IdentityStore.h>

#include <Tactility/Logger.h>
#include <Tactility/file/File.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <string>

#ifdef ESP_PLATFORM
#include <esp_random.h>
#else
#include <random>
#endif

namespace tt::service::reticulum {

static const auto LOGGER = Logger("ReticulumId");

bool IdentityStore::parseSalt(const std::string& text, std::array<uint8_t, 16>& output) {
    std::string hex;
    hex.reserve(text.size());
    for (const auto ch : text) {
        if (!std::isspace(static_cast<unsigned char>(ch))) {
            hex += ch;
        }
    }

    if (hex.size() != output.size() * 2) {
        return false;
    }

    for (size_t i = 0; i < output.size(); i++) {
        const auto high = hex[i * 2];
        const auto low = hex[i * 2 + 1];
        if (!std::isxdigit(static_cast<unsigned char>(high)) || !std::isxdigit(static_cast<unsigned char>(low))) {
            return false;
        }

        const auto hexByte = hex.substr(i * 2, 2);
        output[i] = static_cast<uint8_t>(std::stoul(hexByte, nullptr, 16));
    }

    return true;
}

std::string IdentityStore::saltToHex(const std::array<uint8_t, 16>& salt) {
    std::string output;
    output.reserve(salt.size() * 2);
    for (const auto byte : salt) {
        output += std::format("{:02x}", byte);
    }
    return output;
}

std::array<uint8_t, 16> IdentityStore::generateSalt() {
    std::array<uint8_t, 16> salt {};

#ifdef ESP_PLATFORM
    for (size_t i = 0; i < salt.size(); i += sizeof(uint32_t)) {
        const auto word = esp_random();
        for (size_t j = 0; j < sizeof(uint32_t) && (i + j) < salt.size(); j++) {
            salt[i + j] = static_cast<uint8_t>((word >> (j * 8)) & 0xFF);
        }
    }
#else
    std::random_device randomDevice;
    for (auto& byte : salt) {
        byte = static_cast<uint8_t>(randomDevice());
    }
#endif

    return salt;
}

bool IdentityStore::init(const ServicePaths& paths) {
    rootPath = paths.getUserDataDirectory();
    stateFilePath = paths.getUserDataPath("bootstrap_salt.txt");

    if (!file::findOrCreateDirectory(rootPath, 0777)) {
        LOGGER.error("Failed to create {}", rootPath);
        return false;
    }

    if (file::isFile(stateFilePath)) {
        auto saltText = file::readString(stateFilePath);
        if (saltText == nullptr) {
            LOGGER.error("Failed to read {}", stateFilePath);
            return false;
        }

        if (!parseSalt(reinterpret_cast<const char*>(saltText.get()), bootstrapIdentity.nodeSalt)) {
            LOGGER.error("Invalid bootstrap salt in {}", stateFilePath);
            return false;
        }

        LOGGER.info("Loaded bootstrap identity salt");
    } else {
        bootstrapIdentity.nodeSalt = generateSalt();
        if (!file::writeString(stateFilePath, saltToHex(bootstrapIdentity.nodeSalt))) {
            LOGGER.error("Failed to persist {}", stateFilePath);
            return false;
        }
        LOGGER.info("Created bootstrap identity salt");
    }

    initialized = true;
    return true;
}

} // namespace tt::service::reticulum
