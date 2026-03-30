#pragma once

#include <Tactility/service/ServicePaths.h>

#include <array>
#include <string>

namespace tt::service::reticulum {

class IdentityStore final {

public:

    struct BootstrapIdentity {
        std::array<uint8_t, 16> nodeSalt {};
        bool provisional = true;
    };

private:

    std::string rootPath {};
    std::string stateFilePath {};
    BootstrapIdentity bootstrapIdentity {};
    bool initialized = false;

    static bool parseSalt(const std::string& text, std::array<uint8_t, 16>& output);

    static std::string saltToHex(const std::array<uint8_t, 16>& salt);

    static std::array<uint8_t, 16> generateSalt();

public:

    bool init(const ServicePaths& paths);

    bool isInitialized() const { return initialized; }

    const std::string& getRootPath() const { return rootPath; }

    const std::string& getStateFilePath() const { return stateFilePath; }

    const BootstrapIdentity& getBootstrapIdentity() const { return bootstrapIdentity; }
};

} // namespace tt::service::reticulum
