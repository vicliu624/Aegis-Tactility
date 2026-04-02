#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tt::service::reticulum {

constexpr size_t DESTINATION_HASH_LENGTH = 16;
constexpr size_t FULL_HASH_LENGTH = 32;
constexpr size_t RESOURCE_HASH_LENGTH = 32;
constexpr size_t NAME_HASH_LENGTH = 10;
constexpr size_t CURVE25519_KEY_LENGTH = 32;
constexpr size_t IDENTITY_PUBLIC_KEY_LENGTH = CURVE25519_KEY_LENGTH * 2;
constexpr size_t SIGNATURE_LENGTH = 64;
constexpr size_t ANNOUNCE_RANDOM_LENGTH = 10;
constexpr size_t LINK_SIGNAL_LENGTH = 3;
constexpr size_t PATH_REQUEST_TAG_MAX_LENGTH = 16;
constexpr uint32_t RETICULUM_MTU = 500;
constexpr uint32_t RETICULUM_MDU = 464;

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

enum class HeaderType : uint8_t {
    Header1 = 0x00,
    Header2 = 0x01
};

enum class TransportType : uint8_t {
    Broadcast = 0x00,
    Transport = 0x01
};

enum class PacketType : uint8_t {
    Data = 0x00,
    Announce = 0x01,
    LinkRequest = 0x02,
    Proof = 0x03
};

enum class DestinationType : uint8_t {
    Single = 0x00,
    Group = 0x01,
    Plain = 0x02,
    Link = 0x03
};

enum class PacketContext : uint8_t {
    None = 0x00,
    Resource = 0x01,
    ResourceAdv = 0x02,
    ResourceReq = 0x03,
    ResourceHmu = 0x04,
    ResourcePrf = 0x05,
    ResourceIcl = 0x06,
    ResourceRcl = 0x07,
    CacheRequest = 0x08,
    Request = 0x09,
    Response = 0x0A,
    PathResponse = 0x0B,
    Command = 0x0C,
    CommandStatus = 0x0D,
    Channel = 0x0E,
    Keepalive = 0xFA,
    LinkIdentify = 0xFB,
    LinkClose = 0xFC,
    LinkProof = 0xFD,
    LrRtt = 0xFE,
    LrProof = 0xFF
};

enum class LinkMode : uint8_t {
    Aes128Cbc = 0x00,
    Aes256Cbc = 0x01,
    Aes256Gcm = 0x02,
    Unknown = 0xFF
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
using FullHashBytes = std::array<uint8_t, FULL_HASH_LENGTH>;
using ResourceHashBytes = std::array<uint8_t, RESOURCE_HASH_LENGTH>;
using NameHashBytes = std::array<uint8_t, NAME_HASH_LENGTH>;
using Curve25519PublicKeyBytes = std::array<uint8_t, CURVE25519_KEY_LENGTH>;
using Curve25519PrivateKeyBytes = std::array<uint8_t, CURVE25519_KEY_LENGTH>;
using Ed25519PublicKeyBytes = std::array<uint8_t, CURVE25519_KEY_LENGTH>;
using Ed25519PrivateKeyBytes = std::array<uint8_t, CURVE25519_KEY_LENGTH>;
using IdentityPublicKeyBytes = std::array<uint8_t, IDENTITY_PUBLIC_KEY_LENGTH>;
using SignatureBytes = std::array<uint8_t, SIGNATURE_LENGTH>;
using AnnounceRandomBytes = std::array<uint8_t, ANNOUNCE_RANDOM_LENGTH>;
using LinkSignalBytes = std::array<uint8_t, LINK_SIGNAL_LENGTH>;

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
    DestinationType type = DestinationType::Single;
    std::vector<uint8_t> appData {};
    bool acceptsLinks = true;
    bool announceEnabled = true;
};

struct RegisteredDestination {
    std::string name {};
    DestinationType type = DestinationType::Single;
    DestinationHash hash {};
    DestinationHash identityHash {};
    NameHashBytes nameHash {};
    IdentityPublicKeyBytes identityPublicKey {};
    std::vector<uint8_t> appData {};
    bool acceptsLinks = true;
    bool announceEnabled = true;
};

struct PacketSummary {
    uint8_t flags = 0;
    uint8_t hops = 0;
    std::optional<uint8_t> context {};
    size_t rawSize = 0;
    size_t payloadSize = 0;
    HeaderType headerType = HeaderType::Header1;
    TransportType transportType = TransportType::Broadcast;
    DestinationType destinationType = DestinationType::Single;
    PacketType packetType = PacketType::Data;
    DestinationHash destination {};
    std::optional<DestinationHash> transportId {};
    FullHashBytes packetHash {};
    DestinationHash truncatedPacketHash {};
};

struct PathEntry {
    DestinationHash destination {};
    DestinationHash identityHash {};
    std::string interfaceId {};
    std::vector<uint8_t> nextHop {};
    uint8_t hops = 0;
    uint32_t expiryTick = 0;
    uint32_t observedTick = 0;
    AnnounceRandomBytes announceRandom {};
    FullHashBytes packetHash {};
    bool unresponsive = false;
};

struct LinkInfo {
    DestinationHash linkId {};
    DestinationHash peerDestination {};
    DestinationHash peerIdentityHash {};
    LinkState state = LinkState::Closed;
    LinkMode mode = LinkMode::Aes256Cbc;
    std::string interfaceId {};
    uint32_t negotiatedMtu = RETICULUM_MTU;
    uint32_t lastActivityTick = 0;
    uint32_t lastRttTick = 0;
    bool initiator = false;
    bool remoteIdentified = false;
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
    DestinationHash identityHash {};
    NameHashBytes nameHash {};
    IdentityPublicKeyBytes identityPublicKey {};
    AnnounceRandomBytes announceRandom {};
    std::optional<Curve25519PublicKeyBytes> ratchetPublicKey {};
    FullHashBytes packetHash {};
    SignatureBytes signature {};
    std::string interfaceId {};
    InterfaceKind interfaceKind = InterfaceKind::Unknown;
    std::vector<uint8_t> nextHop {};
    std::vector<uint8_t> appData {};
    uint8_t hops = 0;
    uint8_t context = static_cast<uint8_t>(PacketContext::None);
    bool local = false;
    bool pathResponse = false;
    bool validated = false;
    uint32_t observedTick = 0;
};

struct PathRequestInfo {
    DestinationHash requestDestination {};
    DestinationHash requestedDestination {};
    std::optional<DestinationHash> requestingTransportId {};
    std::vector<uint8_t> tag {};
    std::string interfaceId {};
    InterfaceKind interfaceKind = InterfaceKind::Unknown;
    std::vector<uint8_t> nextHop {};
    uint8_t hops = 0;
    FullHashBytes duplicateKey {};
};

struct LinkRequestInfo {
    DestinationHash destination {};
    DestinationHash linkId {};
    Curve25519PublicKeyBytes initiatorLinkPublic {};
    Ed25519PublicKeyBytes initiatorLinkSignaturePublic {};
    std::optional<LinkSignalBytes> signalling {};
    LinkMode mode = LinkMode::Aes256Cbc;
    uint32_t requestedMtu = RETICULUM_MTU;
    std::string interfaceId {};
    InterfaceKind interfaceKind = InterfaceKind::Unknown;
    std::vector<uint8_t> nextHop {};
    uint8_t hops = 0;
};

struct LinkProofInfo {
    DestinationHash linkId {};
    SignatureBytes signature {};
    Curve25519PublicKeyBytes responderLinkPublic {};
    std::optional<LinkSignalBytes> signalling {};
    LinkMode mode = LinkMode::Aes256Cbc;
    uint32_t confirmedMtu = RETICULUM_MTU;
    std::string interfaceId {};
    InterfaceKind interfaceKind = InterfaceKind::Unknown;
    std::vector<uint8_t> nextHop {};
    uint8_t hops = 0;
};

struct PacketHeader {
    HeaderType headerType = HeaderType::Header1;
    TransportType transportType = TransportType::Broadcast;
    DestinationType destinationType = DestinationType::Single;
    PacketType packetType = PacketType::Data;
    uint8_t context = static_cast<uint8_t>(PacketContext::None);
    bool contextFlag = false;
    uint8_t hops = 0;
    DestinationHash destination {};
    std::optional<DestinationHash> transportId {};
};

const char* runtimeStateToString(RuntimeState state);
const char* interfaceKindToString(InterfaceKind kind);
const char* linkStateToString(LinkState state);
const char* resourceStateToString(ResourceState state);
const char* headerTypeToString(HeaderType type);
const char* transportTypeToString(TransportType type);
const char* packetTypeToString(PacketType type);
const char* destinationTypeToString(DestinationType type);
const char* linkModeToString(LinkMode mode);
const char* packetContextToString(uint8_t context);

std::string toHex(const DestinationHash& hash);

template <size_t N>
std::string toHex(const std::array<uint8_t, N>& bytes) {
    static constexpr char HEX[] = "0123456789abcdef";

    std::string output;
    output.reserve(N * 2);
    for (const auto byte : bytes) {
        output.push_back(HEX[(byte >> 4) & 0x0F]);
        output.push_back(HEX[byte & 0x0F]);
    }

    return output;
}

bool parseHex(std::string_view text, uint8_t* output, size_t outputSize);

} // namespace tt::service::reticulum
