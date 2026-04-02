#pragma once

#include <Tactility/RecursiveMutex.h>
#include <Tactility/service/reticulum/IdentityStore.h>
#include <Tactility/service/reticulum/Types.h>

#include <optional>
#include <vector>

namespace tt::service::reticulum {

class DestinationRegistry final {

    mutable RecursiveMutex mutex;
    std::vector<RegisteredDestination> localDestinations {};

public:

    static bool deriveNameHash(const std::string& name, NameHashBytes& output);

    static bool deriveIdentityHash(const IdentityPublicKeyBytes& publicKey, DestinationHash& output);

    static bool deriveDestinationHash(
        DestinationType type,
        const NameHashBytes& nameHash,
        const DestinationHash& identityHash,
        DestinationHash& output
    );

    bool registerLocalDestination(
        const LocalDestination& destination,
        const IdentityStore::LocalIdentity& localIdentity
    );

    bool updateLocalDestinationAppData(
        const DestinationHash& destinationHash,
        const std::vector<uint8_t>& appData
    );

    std::vector<RegisteredDestination> getLocalDestinations() const;

    std::optional<RegisteredDestination> findLocalDestination(const DestinationHash& destinationHash) const;
};

} // namespace tt::service::reticulum
