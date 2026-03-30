#pragma once

#include <Tactility/RecursiveMutex.h>
#include <Tactility/service/reticulum/Types.h>
#include <Tactility/service/reticulum/IdentityStore.h>

#include <optional>
#include <vector>

namespace tt::service::reticulum {

class DestinationRegistry final {

    mutable RecursiveMutex mutex;
    std::vector<RegisteredDestination> localDestinations {};
    std::vector<AppEndpoint> appEndpoints {};

    static DestinationHash deriveProvisionalHash(
        const IdentityStore::BootstrapIdentity& bootstrapIdentity,
        const std::string& name
    );

    static DestinationHash deriveSharedEndpointHash(const std::string& endpointName);

public:

    bool registerLocalDestination(
        const LocalDestination& destination,
        const IdentityStore::BootstrapIdentity& bootstrapIdentity
    );

    std::vector<RegisteredDestination> getLocalDestinations() const;

    bool registerAppEndpoint(const std::string& endpointName);

    std::vector<AppEndpoint> getAppEndpoints() const;

    std::optional<AppEndpoint> findAppEndpoint(const std::string& endpointName) const;

    std::optional<AppEndpoint> findAppEndpoint(const DestinationHash& hash) const;
};

} // namespace tt::service::reticulum
