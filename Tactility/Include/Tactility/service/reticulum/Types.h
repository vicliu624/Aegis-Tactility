#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tt::service::reticulum {

constexpr size_t DESTINATION_HASH_LENGTH = 16;
constexpr size_t RESOURCE_HASH_LENGTH = 32;

enum class RuntimeState {
    Starting,
    Ready,
    Stopping,
    Stopped,
    Faulted
};

enum class InterfaceKind {
    Unknown,
    EspNow,
    LoRa
};

enum class LinkState {
    Pending,
    Handshake,
    Active,
    Stale,
    Closed
};

enum class ResourceState {
    None,
    Queued,
    Advertised,
    Transferring,
    AwaitingProof,
    Assembling,
    Complete,
    Failed,
    Corrupt,
    Rejected
};

enum class InterfaceCapability : uint32_t {
    None = 0,
    Broadcast = 1U << 0,
    Transport = 1U << 1,
    Link = 1U << 2,
    Resource = 1U << 3,
    Metrics = 1U << 4
};

constexpr uint32_t toMask(InterfaceCapability capability) {
    return static_cast<uint32_t>(capability);
}

constexpr uint32_t operator|(InterfaceCapability left, InterfaceCapability right) {
    return toMask(left) | toMask(right);
}

constexpr uint32_t operator|(uint32_t left, InterfaceCapability right) {
    return left | toMask(right);
}

constexpr bool hasCapability(uint32_t mask, InterfaceCapability capability) {
    return (mask & toMask(capability)) == toMask(capability);
}

using DestinationHashBytes = std::array<uint8_t, DESTINATION_HASH_LENGTH>;
using ResourceHashBytes = std::array<uint8_t, RESOURCE_HASH_LENGTH>;

struct DestinationHash {
    DestinationHashBytes bytes {};

    bool empty() const;

    bool operator==(const DestinationHash& other) const = default;
};

struct InterfaceMetrics {
    bool available = false;
    int32_t bitrate = 0;
    int16_t rssi = 0;
    int16_t snr = 0;
    size_t hardwareMtu = 0;
};

struct InterfaceDescriptor {
    std::string id {};
    InterfaceKind kind = InterfaceKind::Unknown;
    uint32_t capabilities = toMask(InterfaceCapability::None);
    bool started = false;
    InterfaceMetrics metrics {};
};

struct InterfaceFrame {
    std::vector<uint8_t> payload {};
    std::vector<uint8_t> nextHop {};
    bool broadcast = false;
};

struct InboundFrame {
    std::string interfaceId {};
    InterfaceKind interfaceKind = InterfaceKind::Unknown;
    InterfaceMetrics metrics {};
    std::vector<uint8_t> nextHop {};
    std::vector<uint8_t> payload {};
};

struct LocalDestination {
    std::string name {};
    std::vector<uint8_t> appData {};
    bool acceptsLinks = true;
    bool announceEnabled = true;
};

struct RegisteredDestination {
    std::string name {};
    DestinationHash hash {};
    std::vector<uint8_t> appData {};
    bool acceptsLinks = true;
    bool announceEnabled = true;
    bool provisionalHash = true;
};

struct PathEntry {
    DestinationHash destination {};
    std::string interfaceId {};
    std::vector<uint8_t> nextHop {};
    uint8_t hops = 0;
    uint32_t expiryTick = 0;
    bool unresponsive = false;
};

struct LinkInfo {
    DestinationHash linkId {};
    DestinationHash peerDestination {};
    LinkState state = LinkState::Closed;
    std::string interfaceId {};
    uint32_t negotiatedMtu = 500;
};

struct ResourceInfo {
    ResourceHashBytes resourceHash {};
    DestinationHash linkId {};
    ResourceState state = ResourceState::None;
    size_t transferSize = 0;
    size_t totalSize = 0;
};

struct AnnounceInfo {
    DestinationHash destination {};
    std::string interfaceId {};
    InterfaceKind interfaceKind = InterfaceKind::Unknown;
    std::vector<uint8_t> nextHop {};
    std::vector<uint8_t> appData {};
    uint8_t hops = 0;
    uint8_t context = 0;
    bool local = false;
    bool pathResponse = false;
    bool provisional = true;
    uint32_t observedTick = 0;
};

struct PacketSummary {
    uint8_t flags = 0;
    uint8_t hops = 0;
    std::optional<uint8_t> context {};
    size_t rawSize = 0;
    size_t payloadSize = 0;
};

const char* runtimeStateToString(RuntimeState state);
const char* interfaceKindToString(InterfaceKind kind);
const char* linkStateToString(LinkState state);
const char* resourceStateToString(ResourceState state);

std::string toHex(const DestinationHash& hash);
std::string toHex(const ResourceHashBytes& hash);

} // namespace tt::service::reticulum
