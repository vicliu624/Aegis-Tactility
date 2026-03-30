#pragma once

#include <Tactility/RecursiveMutex.h>
#include <Tactility/service/reticulum/Types.h>
#include <Tactility/service/reticulum/IdentityStore.h>

#include <vector>

namespace tt::service::reticulum {

class DestinationRegistry final {

    mutable RecursiveMutex mutex;
    std::vector<RegisteredDestination> localDestinations {};

    static DestinationHash deriveProvisionalHash(
        const IdentityStore::BootstrapIdentity& bootstrapIdentity,
        const std::string& name
    );

public:

    bool registerLocalDestination(
        const LocalDestination& destination,
        const IdentityStore::BootstrapIdentity& bootstrapIdentity
    );

    std::vector<RegisteredDestination> getLocalDestinations() const;
};

} // namespace tt::service::reticulum
