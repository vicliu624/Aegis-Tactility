#pragma once

#include <Tactility/RecursiveMutex.h>
#include <Tactility/service/ServicePaths.h>
#include <Tactility/service/reticulum/Types.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace tt::service::reticulum {

class IdentityStore final {

public:

    struct LocalIdentity {
        DestinationHash hash {};
        Curve25519PrivateKeyBytes encryptionPrivateKey {};
        Curve25519PublicKeyBytes encryptionPublicKey {};
        Ed25519PrivateKeyBytes signingPrivateKey {};
        Ed25519PublicKeyBytes signingPublicKey {};
        IdentityPublicKeyBytes publicKey {};
    };

    struct KnownDestination {
        DestinationHash destinationHash {};
        DestinationHash identityHash {};
        NameHashBytes nameHash {};
        IdentityPublicKeyBytes identityPublicKey {};
        std::vector<uint8_t> appData {};
        std::optional<Curve25519PublicKeyBytes> latestRatchetPublicKey {};
        FullHashBytes lastAnnouncePacketHash {};
        AnnounceRandomBytes lastAnnounceRandom {};
        uint32_t lastSeenTick = 0;
    };

private:

    mutable RecursiveMutex mutex;
    std::string rootPath {};
    std::string localIdentityPath {};
    std::string knownDestinationsPath {};
    LocalIdentity localIdentity {};
    std::unordered_map<std::string, KnownDestination> knownDestinations {};
    bool initialized = false;

    bool loadOrCreateLocalIdentity();

    bool loadKnownDestinations();

    bool persistLocalIdentity() const;

    bool persistKnownDestinationsLocked() const;

    static std::string keyFor(const DestinationHash& destinationHash);

public:

    bool init(const ServicePaths& paths);

    bool isInitialized() const { return initialized; }

    const std::string& getRootPath() const { return rootPath; }

    const std::string& getLocalIdentityPath() const { return localIdentityPath; }

    const std::string& getKnownDestinationsPath() const { return knownDestinationsPath; }

    LocalIdentity getLocalIdentity() const;

    std::optional<KnownDestination> getKnownDestination(const DestinationHash& destinationHash) const;

    std::optional<KnownDestination> getKnownDestinationByIdentityHash(const DestinationHash& identityHash) const;

    std::vector<KnownDestination> getKnownDestinations() const;

    bool rememberDestination(const KnownDestination& destination);

    bool updateRatchet(const DestinationHash& destinationHash, const Curve25519PublicKeyBytes& ratchetPublicKey);
};

} // namespace tt::service::reticulum
