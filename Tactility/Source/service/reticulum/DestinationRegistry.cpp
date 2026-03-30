#include <Tactility/service/reticulum/DestinationRegistry.h>

#include <array>
#include <string_view>

namespace tt::service::reticulum {

static constexpr uint64_t FNV_OFFSET = 14695981039346656037ULL;
static constexpr uint64_t FNV_PRIME = 1099511628211ULL;

static uint64_t fnv1aUpdate(uint64_t hash, uint8_t byte) {
    hash ^= byte;
    hash *= FNV_PRIME;
    return hash;
}

DestinationHash DestinationRegistry::deriveProvisionalHash(
    const IdentityStore::BootstrapIdentity& bootstrapIdentity,
    const std::string& name
) {
    // This milestone uses a stable provisional hash so the service can expose
    // durable destination records before the full Ed25519/X25519 identity stack lands.
    uint64_t first = FNV_OFFSET;
    for (const auto byte : bootstrapIdentity.nodeSalt) {
        first = fnv1aUpdate(first, byte);
    }
    for (const auto ch : name) {
        first = fnv1aUpdate(first, static_cast<uint8_t>(ch));
    }

    uint64_t second = FNV_OFFSET ^ 0x9E3779B97F4A7C15ULL;
    for (auto it = name.rbegin(); it != name.rend(); ++it) {
        second = fnv1aUpdate(second, static_cast<uint8_t>(*it));
    }
    for (const auto byte : bootstrapIdentity.nodeSalt) {
        second = fnv1aUpdate(second, static_cast<uint8_t>(byte ^ 0xA5));
    }

    DestinationHash output;
    for (size_t i = 0; i < 8; i++) {
        output.bytes[i] = static_cast<uint8_t>((first >> (i * 8)) & 0xFF);
        output.bytes[8 + i] = static_cast<uint8_t>((second >> (i * 8)) & 0xFF);
    }
    return output;
}

DestinationHash DestinationRegistry::deriveSharedEndpointHash(const std::string& endpointName) {
    uint64_t first = FNV_OFFSET;
    for (const auto ch : std::string_view("reticulum-app-endpoint:")) {
        first = fnv1aUpdate(first, static_cast<uint8_t>(ch));
    }
    for (const auto ch : endpointName) {
        first = fnv1aUpdate(first, static_cast<uint8_t>(ch));
    }

    uint64_t second = FNV_OFFSET ^ 0xD6E8FEB86659FD93ULL;
    for (const auto ch : std::string_view("shared-endpoint")) {
        second = fnv1aUpdate(second, static_cast<uint8_t>(ch));
    }
    for (auto it = endpointName.rbegin(); it != endpointName.rend(); ++it) {
        second = fnv1aUpdate(second, static_cast<uint8_t>(*it));
    }

    DestinationHash output;
    for (size_t i = 0; i < 8; i++) {
        output.bytes[i] = static_cast<uint8_t>((first >> (i * 8)) & 0xFF);
        output.bytes[8 + i] = static_cast<uint8_t>((second >> (i * 8)) & 0xFF);
    }
    return output;
}

bool DestinationRegistry::registerLocalDestination(
    const LocalDestination& destination,
    const IdentityStore::BootstrapIdentity& bootstrapIdentity
) {
    if (destination.name.empty()) {
        return false;
    }

    auto lock = mutex.asScopedLock();
    lock.lock();

    for (const auto& existing : localDestinations) {
        if (existing.name == destination.name) {
            return false;
        }
    }

    localDestinations.push_back(RegisteredDestination {
        .name = destination.name,
        .hash = deriveProvisionalHash(bootstrapIdentity, destination.name),
        .appData = destination.appData,
        .acceptsLinks = destination.acceptsLinks,
        .announceEnabled = destination.announceEnabled,
        .provisionalHash = true
    });

    return true;
}

std::vector<RegisteredDestination> DestinationRegistry::getLocalDestinations() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return localDestinations;
}

bool DestinationRegistry::registerAppEndpoint(const std::string& endpointName) {
    if (endpointName.empty()) {
        return false;
    }

    auto lock = mutex.asScopedLock();
    lock.lock();

    for (const auto& existing : appEndpoints) {
        if (existing.name == endpointName) {
            return false;
        }
    }

    appEndpoints.push_back(AppEndpoint {
        .name = endpointName,
        .hash = deriveSharedEndpointHash(endpointName),
        .provisionalHash = true
    });

    return true;
}

std::vector<AppEndpoint> DestinationRegistry::getAppEndpoints() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return appEndpoints;
}

std::optional<AppEndpoint> DestinationRegistry::findAppEndpoint(const std::string& endpointName) const {
    auto lock = mutex.asScopedLock();
    lock.lock();

    for (const auto& endpoint : appEndpoints) {
        if (endpoint.name == endpointName) {
            return endpoint;
        }
    }

    return std::nullopt;
}

std::optional<AppEndpoint> DestinationRegistry::findAppEndpoint(const DestinationHash& hash) const {
    auto lock = mutex.asScopedLock();
    lock.lock();

    for (const auto& endpoint : appEndpoints) {
        if (endpoint.hash == hash) {
            return endpoint;
        }
    }

    return std::nullopt;
}

} // namespace tt::service::reticulum
