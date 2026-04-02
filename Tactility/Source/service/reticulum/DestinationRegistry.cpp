#include <Tactility/service/reticulum/DestinationRegistry.h>

#include <Tactility/service/reticulum/Crypto.h>

#include <algorithm>

namespace tt::service::reticulum {

bool DestinationRegistry::deriveNameHash(const std::string& name, NameHashBytes& output) {
    FullHashBytes fullHash {};
    if (!crypto::sha256(reinterpret_cast<const uint8_t*>(name.data()), name.size(), fullHash)) {
        return false;
    }

    std::copy_n(fullHash.begin(), output.size(), output.begin());
    return true;
}

bool DestinationRegistry::deriveIdentityHash(const IdentityPublicKeyBytes& publicKey, DestinationHash& output) {
    FullHashBytes fullHash {};
    if (!crypto::sha256(publicKey.data(), publicKey.size(), fullHash)) {
        return false;
    }

    std::copy_n(fullHash.begin(), output.bytes.size(), output.bytes.begin());
    return true;
}

bool DestinationRegistry::deriveDestinationHash(
    DestinationType type,
    const NameHashBytes& nameHash,
    const DestinationHash& identityHash,
    DestinationHash& output
) {
    std::array<uint8_t, NAME_HASH_LENGTH + DESTINATION_HASH_LENGTH> material {};
    size_t materialSize = nameHash.size();
    std::copy(nameHash.begin(), nameHash.end(), material.begin());

    if (type != DestinationType::Plain) {
        if (identityHash.empty()) {
            return false;
        }

        std::copy(identityHash.bytes.begin(), identityHash.bytes.end(), material.begin() + materialSize);
        materialSize += identityHash.bytes.size();
    }

    FullHashBytes fullHash {};
    if (!crypto::sha256(material.data(), materialSize, fullHash)) {
        return false;
    }

    std::copy_n(fullHash.begin(), output.bytes.size(), output.bytes.begin());
    return true;
}

bool DestinationRegistry::registerLocalDestination(
    const LocalDestination& destination,
    const IdentityStore::LocalIdentity& localIdentity
) {
    if (destination.name.empty()) {
        return false;
    }

    RegisteredDestination record {
        .name = destination.name,
        .type = destination.type,
        .appData = destination.appData,
        .acceptsLinks = destination.acceptsLinks,
        .announceEnabled = destination.announceEnabled
    };

    if (!deriveNameHash(destination.name, record.nameHash)) {
        return false;
    }

    if (destination.type != DestinationType::Plain) {
        record.identityHash = localIdentity.hash;
        record.identityPublicKey = localIdentity.publicKey;
    }

    if (!deriveDestinationHash(destination.type, record.nameHash, record.identityHash, record.hash)) {
        return false;
    }

    auto lock = mutex.asScopedLock();
    lock.lock();

    for (const auto& existing : localDestinations) {
        if (existing.name == destination.name || existing.hash == record.hash) {
            return false;
        }
    }

    localDestinations.push_back(record);
    return true;
}

std::vector<RegisteredDestination> DestinationRegistry::getLocalDestinations() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return localDestinations;
}

std::optional<RegisteredDestination> DestinationRegistry::findLocalDestination(const DestinationHash& destinationHash) const {
    auto lock = mutex.asScopedLock();
    lock.lock();

    const auto iterator = std::find_if(localDestinations.begin(), localDestinations.end(), [&destinationHash](const auto& item) {
        return item.hash == destinationHash;
    });

    if (iterator != localDestinations.end()) {
        return *iterator;
    }

    return std::nullopt;
}

} // namespace tt::service::reticulum
