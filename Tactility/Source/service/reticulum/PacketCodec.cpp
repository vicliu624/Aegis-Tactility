#include <Tactility/service/reticulum/PacketCodec.h>

#include <Tactility/kernel/Kernel.h>
#include <Tactility/service/reticulum/Crypto.h>
#include <Tactility/service/reticulum/DestinationRegistry.h>

#include <algorithm>
#include <array>
#include <ctime>

namespace tt::service::reticulum {

namespace {

constexpr uint8_t FLAG_HEADER_TYPE = 0x40;
constexpr uint8_t FLAG_CONTEXT_FLAG = 0x20;
constexpr uint8_t FLAG_TRANSPORT_TYPE = 0x10;
constexpr uint8_t MASK_DESTINATION_TYPE = 0x0C;
constexpr uint8_t MASK_PACKET_TYPE = 0x03;

Ed25519PublicKeyBytes extractSigningPublicKey(const IdentityPublicKeyBytes& publicKey) {
    Ed25519PublicKeyBytes output {};
    std::copy_n(publicKey.begin() + CURVE25519_KEY_LENGTH, output.size(), output.begin());
    return output;
}

bool buildPacketHash(const std::vector<uint8_t>& packet, HeaderType headerType, FullHashBytes& packetHash, DestinationHash& truncatedHash) {
    if (packet.empty()) {
        return false;
    }

    std::vector<uint8_t> hashable;
    hashable.reserve(packet.size());
    hashable.push_back(static_cast<uint8_t>(packet[0] & 0x0F));

    if (headerType == HeaderType::Header2) {
        if (packet.size() < 18) {
            return false;
        }
        hashable.insert(hashable.end(), packet.begin() + 18, packet.end());
    } else {
        if (packet.size() < 2) {
            return false;
        }
        hashable.insert(hashable.end(), packet.begin() + 2, packet.end());
    }

    if (!crypto::sha256(hashable.data(), hashable.size(), packetHash)) {
        return false;
    }

    std::copy_n(packetHash.begin(), truncatedHash.bytes.size(), truncatedHash.bytes.begin());
    return true;
}

bool appendBytes(std::vector<uint8_t>& output, const uint8_t* data, size_t length) {
    if (data == nullptr && length != 0) {
        return false;
    }

    output.insert(output.end(), data, data + length);
    return true;
}

std::vector<uint8_t> buildAnnounceRandom() {
    std::vector<uint8_t> output(ANNOUNCE_RANDOM_LENGTH, 0);
    if (!crypto::fillRandom(output.data(), 5)) {
        return {};
    }

    const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
    for (size_t index = 0; index < 5; index++) {
        output[5 + index] = static_cast<uint8_t>((now >> ((4 - index) * 8)) & 0xFF);
    }
    return output;
}

uint8_t packFlags(const PacketHeader& header) {
    return (static_cast<uint8_t>(header.headerType) << 6)
        | (header.contextFlag ? FLAG_CONTEXT_FLAG : 0)
        | (static_cast<uint8_t>(header.transportType) << 4)
        | (static_cast<uint8_t>(header.destinationType) << 2)
        | static_cast<uint8_t>(header.packetType);
}

} // namespace

bool PacketCodec::isEnabledLinkMode(LinkMode mode) {
    return mode == LinkMode::Aes256Cbc;
}

DestinationHash PacketCodec::pathRequestDestinationHash() {
    static const DestinationHash hash = [] {
        NameHashBytes nameHash {};
        DestinationHash destinationHash {};
        DestinationRegistry::deriveNameHash("rnstransport.path.request", nameHash);
        DestinationRegistry::deriveDestinationHash(DestinationType::Plain, nameHash, DestinationHash {}, destinationHash);
        return destinationHash;
    }();
    return hash;
}

bool PacketCodec::encodeLinkSignal(LinkMode mode, uint32_t mtu, LinkSignalBytes& output) {
    if (!isEnabledLinkMode(mode) || (mtu & ~0x1FFFFFUL) != 0) {
        return false;
    }

    const uint32_t encoded = (mtu & 0x1FFFFFUL) | ((static_cast<uint32_t>(mode) & 0x07U) << 21);
    output[0] = static_cast<uint8_t>((encoded >> 16) & 0xFF);
    output[1] = static_cast<uint8_t>((encoded >> 8) & 0xFF);
    output[2] = static_cast<uint8_t>(encoded & 0xFF);
    return true;
}

bool PacketCodec::decodeLinkSignal(const LinkSignalBytes& signalling, LinkMode& mode, uint32_t& mtu) {
    const uint32_t encoded = (static_cast<uint32_t>(signalling[0]) << 16)
        | (static_cast<uint32_t>(signalling[1]) << 8)
        | static_cast<uint32_t>(signalling[2]);

    mode = static_cast<LinkMode>((encoded >> 21) & 0x07U);
    mtu = encoded & 0x1FFFFFUL;
    return isEnabledLinkMode(mode);
}

std::optional<DecodedPacket> PacketCodec::decode(const std::vector<uint8_t>& packet) const {
    if (packet.size() < 2 + DESTINATION_HASH_LENGTH + 1) {
        return std::nullopt;
    }

    DecodedPacket decoded;
    decoded.summary.flags = packet[0];
    decoded.summary.hops = packet[1];
    decoded.summary.rawSize = packet.size();

    decoded.header.headerType = (packet[0] & FLAG_HEADER_TYPE) != 0 ? HeaderType::Header2 : HeaderType::Header1;
    decoded.header.contextFlag = (packet[0] & FLAG_CONTEXT_FLAG) != 0;
    decoded.header.transportType = (packet[0] & FLAG_TRANSPORT_TYPE) != 0 ? TransportType::Transport : TransportType::Broadcast;
    decoded.header.destinationType = static_cast<DestinationType>((packet[0] & MASK_DESTINATION_TYPE) >> 2);
    decoded.header.packetType = static_cast<PacketType>(packet[0] & MASK_PACKET_TYPE);
    decoded.header.hops = packet[1];

    size_t offset = 2;
    if (decoded.header.headerType == HeaderType::Header2) {
        if (packet.size() < offset + DESTINATION_HASH_LENGTH * 2 + 1) {
            return std::nullopt;
        }

        DestinationHash transportId;
        std::copy_n(packet.begin() + offset, DESTINATION_HASH_LENGTH, transportId.bytes.begin());
        decoded.header.transportId = transportId;
        offset += DESTINATION_HASH_LENGTH;
    }

    std::copy_n(packet.begin() + offset, DESTINATION_HASH_LENGTH, decoded.header.destination.bytes.begin());
    offset += DESTINATION_HASH_LENGTH;

    if (packet.size() < offset + 1) {
        return std::nullopt;
    }

    decoded.header.context = packet[offset];
    decoded.summary.context = decoded.header.context;
    offset += 1;

    decoded.payload.assign(packet.begin() + offset, packet.end());
    decoded.summary.payloadSize = decoded.payload.size();
    decoded.summary.headerType = decoded.header.headerType;
    decoded.summary.transportType = decoded.header.transportType;
    decoded.summary.destinationType = decoded.header.destinationType;
    decoded.summary.packetType = decoded.header.packetType;
    decoded.summary.destination = decoded.header.destination;
    decoded.summary.transportId = decoded.header.transportId;

    if (!buildPacketHash(packet, decoded.header.headerType, decoded.summary.packetHash, decoded.summary.truncatedPacketHash)) {
        return std::nullopt;
    }

    return decoded;
}

std::optional<PacketSummary> PacketCodec::summarize(const std::vector<uint8_t>& packet) const {
    const auto decoded = decode(packet);
    return decoded.has_value() ? std::optional<PacketSummary>(decoded->summary) : std::nullopt;
}

std::optional<AnnounceInfo> PacketCodec::decodeAnnounce(
    const InboundFrame& frame,
    const DecodedPacket& packet,
    const IdentityPublicKeyBytes* pinnedIdentityPublicKey
) const {
    if (packet.header.packetType != PacketType::Announce ||
        packet.payload.size() < IDENTITY_PUBLIC_KEY_LENGTH + NAME_HASH_LENGTH + ANNOUNCE_RANDOM_LENGTH + SIGNATURE_LENGTH) {
        return std::nullopt;
    }

    const bool hasRatchet = packet.header.contextFlag;
    const size_t minimumSize = IDENTITY_PUBLIC_KEY_LENGTH + NAME_HASH_LENGTH + ANNOUNCE_RANDOM_LENGTH
        + (hasRatchet ? CURVE25519_KEY_LENGTH : 0)
        + SIGNATURE_LENGTH;
    if (packet.payload.size() < minimumSize) {
        return std::nullopt;
    }

    AnnounceInfo announce;
    announce.destination = packet.header.destination;
    announce.interfaceId = frame.interfaceId;
    announce.interfaceKind = frame.interfaceKind;
    announce.nextHop = frame.nextHop;
    announce.hops = packet.header.hops;
    announce.context = packet.header.context;
    announce.pathResponse = packet.header.context == static_cast<uint8_t>(PacketContext::PathResponse);
    announce.observedTick = kernel::getTicks();
    announce.packetHash = packet.summary.packetHash;

    size_t offset = 0;
    std::copy_n(packet.payload.begin() + offset, announce.identityPublicKey.size(), announce.identityPublicKey.begin());
    offset += announce.identityPublicKey.size();

    std::copy_n(packet.payload.begin() + offset, announce.nameHash.size(), announce.nameHash.begin());
    offset += announce.nameHash.size();

    std::copy_n(packet.payload.begin() + offset, announce.announceRandom.size(), announce.announceRandom.begin());
    offset += announce.announceRandom.size();

    if (hasRatchet) {
        Curve25519PublicKeyBytes ratchet {};
        std::copy_n(packet.payload.begin() + offset, ratchet.size(), ratchet.begin());
        announce.ratchetPublicKey = ratchet;
        offset += ratchet.size();
    }

    std::copy_n(packet.payload.begin() + offset, announce.signature.size(), announce.signature.begin());
    offset += announce.signature.size();

    if (packet.payload.size() > offset) {
        announce.appData.assign(packet.payload.begin() + offset, packet.payload.end());
    }

    if (pinnedIdentityPublicKey != nullptr && *pinnedIdentityPublicKey != announce.identityPublicKey) {
        return std::nullopt;
    }

    if (!DestinationRegistry::deriveIdentityHash(announce.identityPublicKey, announce.identityHash)) {
        return std::nullopt;
    }

    DestinationHash expectedDestination {};
    if (!DestinationRegistry::deriveDestinationHash(packet.header.destinationType, announce.nameHash, announce.identityHash, expectedDestination) ||
        expectedDestination != announce.destination) {
        return std::nullopt;
    }

    std::vector<uint8_t> transcript;
    transcript.reserve(
        announce.destination.bytes.size() +
        announce.identityPublicKey.size() +
        announce.nameHash.size() +
        announce.announceRandom.size() +
        (announce.ratchetPublicKey.has_value() ? announce.ratchetPublicKey->size() : 0) +
        announce.appData.size()
    );
    appendBytes(transcript, announce.destination.bytes.data(), announce.destination.bytes.size());
    appendBytes(transcript, announce.identityPublicKey.data(), announce.identityPublicKey.size());
    appendBytes(transcript, announce.nameHash.data(), announce.nameHash.size());
    appendBytes(transcript, announce.announceRandom.data(), announce.announceRandom.size());
    if (announce.ratchetPublicKey.has_value()) {
        appendBytes(transcript, announce.ratchetPublicKey->data(), announce.ratchetPublicKey->size());
    }
    if (!announce.appData.empty()) {
        appendBytes(transcript, announce.appData.data(), announce.appData.size());
    }

    const auto signingPublicKey = extractSigningPublicKey(announce.identityPublicKey);
    if (!crypto::ed25519Verify(signingPublicKey, transcript.data(), transcript.size(), announce.signature)) {
        return std::nullopt;
    }

    announce.validated = true;
    return announce;
}

std::optional<PathRequestInfo> PacketCodec::decodePathRequest(
    const InboundFrame& frame,
    const DecodedPacket& packet
) const {
    if (packet.header.packetType != PacketType::Data ||
        packet.header.destinationType != DestinationType::Plain ||
        packet.header.context != static_cast<uint8_t>(PacketContext::None) ||
        packet.header.destination != pathRequestDestinationHash() ||
        packet.payload.size() <= DESTINATION_HASH_LENGTH) {
        return std::nullopt;
    }

    PathRequestInfo request;
    request.requestDestination = packet.header.destination;
    request.interfaceId = frame.interfaceId;
    request.interfaceKind = frame.interfaceKind;
    request.nextHop = frame.nextHop;
    request.hops = packet.header.hops;

    size_t offset = 0;
    std::copy_n(packet.payload.begin(), request.requestedDestination.bytes.size(), request.requestedDestination.bytes.begin());
    offset += request.requestedDestination.bytes.size();

    if (packet.payload.size() > DESTINATION_HASH_LENGTH + PATH_REQUEST_TAG_MAX_LENGTH) {
        DestinationHash transportId {};
        std::copy_n(packet.payload.begin() + offset, transportId.bytes.size(), transportId.bytes.begin());
        request.requestingTransportId = transportId;
        offset += transportId.bytes.size();
    }

    if (packet.payload.size() <= offset) {
        return std::nullopt;
    }

    request.tag.assign(packet.payload.begin() + offset, packet.payload.end());
    if (request.tag.empty()) {
        return std::nullopt;
    }
    if (request.tag.size() > PATH_REQUEST_TAG_MAX_LENGTH) {
        request.tag.resize(PATH_REQUEST_TAG_MAX_LENGTH);
    }

    std::array<uint8_t, DESTINATION_HASH_LENGTH + PATH_REQUEST_TAG_MAX_LENGTH> duplicateMaterial {};
    std::copy(request.requestedDestination.bytes.begin(), request.requestedDestination.bytes.end(), duplicateMaterial.begin());
    std::copy(request.tag.begin(), request.tag.end(), duplicateMaterial.begin() + DESTINATION_HASH_LENGTH);
    FullHashBytes duplicateHash {};
    if (!crypto::sha256(duplicateMaterial.data(), duplicateMaterial.size(), duplicateHash)) {
        return std::nullopt;
    }
    request.duplicateKey = duplicateHash;

    return request;
}

std::optional<LinkRequestInfo> PacketCodec::decodeLinkRequest(
    const InboundFrame& frame,
    const DecodedPacket& packet,
    DestinationHash& derivedLinkId
) const {
    if (packet.header.packetType != PacketType::LinkRequest ||
        packet.header.context != static_cast<uint8_t>(PacketContext::None) ||
        (packet.payload.size() != CURVE25519_KEY_LENGTH * 2 && packet.payload.size() != CURVE25519_KEY_LENGTH * 2 + LINK_SIGNAL_LENGTH)) {
        return std::nullopt;
    }

    if (!deriveLinkId(frame.payload, packet, derivedLinkId)) {
        return std::nullopt;
    }

    LinkRequestInfo request;
    request.destination = packet.header.destination;
    request.linkId = derivedLinkId;
    request.interfaceId = frame.interfaceId;
    request.interfaceKind = frame.interfaceKind;
    request.nextHop = frame.nextHop;
    request.hops = packet.header.hops;

    size_t offset = 0;
    std::copy_n(packet.payload.begin() + offset, request.initiatorLinkPublic.size(), request.initiatorLinkPublic.begin());
    offset += request.initiatorLinkPublic.size();

    std::copy_n(packet.payload.begin() + offset, request.initiatorLinkSignaturePublic.size(), request.initiatorLinkSignaturePublic.begin());
    offset += request.initiatorLinkSignaturePublic.size();

    if (packet.payload.size() == offset + LINK_SIGNAL_LENGTH) {
        LinkSignalBytes signalling {};
        std::copy_n(packet.payload.begin() + offset, signalling.size(), signalling.begin());
        LinkMode mode = LinkMode::Unknown;
        uint32_t mtu = RETICULUM_MTU;
        if (!decodeLinkSignal(signalling, mode, mtu)) {
            return std::nullopt;
        }

        request.signalling = signalling;
        request.mode = mode;
        request.requestedMtu = mtu;
    } else {
        request.mode = LinkMode::Aes256Cbc;
        request.requestedMtu = RETICULUM_MTU;
    }

    return request;
}

std::optional<LinkProofInfo> PacketCodec::decodeLinkProof(
    const InboundFrame& frame,
    const DecodedPacket& packet
) const {
    if (packet.header.packetType != PacketType::Proof ||
        packet.header.destinationType != DestinationType::Link ||
        packet.header.context != static_cast<uint8_t>(PacketContext::LrProof) ||
        (packet.payload.size() != SIGNATURE_LENGTH + CURVE25519_KEY_LENGTH &&
         packet.payload.size() != SIGNATURE_LENGTH + CURVE25519_KEY_LENGTH + LINK_SIGNAL_LENGTH)) {
        return std::nullopt;
    }

    LinkProofInfo proof;
    proof.linkId = packet.header.destination;
    proof.interfaceId = frame.interfaceId;
    proof.interfaceKind = frame.interfaceKind;
    proof.nextHop = frame.nextHop;
    proof.hops = packet.header.hops;

    size_t offset = 0;
    std::copy_n(packet.payload.begin() + offset, proof.signature.size(), proof.signature.begin());
    offset += proof.signature.size();

    std::copy_n(packet.payload.begin() + offset, proof.responderLinkPublic.size(), proof.responderLinkPublic.begin());
    offset += proof.responderLinkPublic.size();

    if (packet.payload.size() == offset + LINK_SIGNAL_LENGTH) {
        LinkSignalBytes signalling {};
        std::copy_n(packet.payload.begin() + offset, signalling.size(), signalling.begin());
        LinkMode mode = LinkMode::Unknown;
        uint32_t mtu = RETICULUM_MTU;
        if (!decodeLinkSignal(signalling, mode, mtu)) {
            return std::nullopt;
        }

        proof.signalling = signalling;
        proof.mode = mode;
        proof.confirmedMtu = mtu;
    } else {
        proof.mode = LinkMode::Aes256Cbc;
        proof.confirmedMtu = RETICULUM_MTU;
    }

    return proof;
}

bool PacketCodec::deriveLinkId(const std::vector<uint8_t>& rawPacket, const DecodedPacket& packet, DestinationHash& output) const {
    if (packet.header.packetType != PacketType::LinkRequest || rawPacket.size() < 2 + DESTINATION_HASH_LENGTH + 1 + CURVE25519_KEY_LENGTH * 2) {
        return false;
    }

    std::vector<uint8_t> hashable;
    hashable.reserve(rawPacket.size());
    hashable.push_back(static_cast<uint8_t>(rawPacket[0] & 0x0F));
    hashable.insert(hashable.end(), rawPacket.begin() + 2, rawPacket.end());

    if (packet.payload.size() > CURVE25519_KEY_LENGTH * 2) {
        hashable.resize(hashable.size() - (packet.payload.size() - CURVE25519_KEY_LENGTH * 2));
    }

    FullHashBytes fullHash {};
    if (!crypto::sha256(hashable.data(), hashable.size(), fullHash)) {
        return false;
    }

    std::copy_n(fullHash.begin(), output.bytes.size(), output.bytes.begin());
    return true;
}

std::vector<uint8_t> PacketCodec::encodePacket(const PacketHeader& header, const std::vector<uint8_t>& payload) const {
    std::vector<uint8_t> output;
    const size_t addressBlockSize = header.headerType == HeaderType::Header2
        ? DESTINATION_HASH_LENGTH * 2
        : DESTINATION_HASH_LENGTH;
    output.reserve(2 + addressBlockSize + 1 + payload.size());

    output.push_back(packFlags(header));
    output.push_back(header.hops);

    if (header.headerType == HeaderType::Header2) {
        if (!header.transportId.has_value()) {
            return {};
        }
        output.insert(output.end(), header.transportId->bytes.begin(), header.transportId->bytes.end());
    }

    output.insert(output.end(), header.destination.bytes.begin(), header.destination.bytes.end());
    output.push_back(header.context);
    output.insert(output.end(), payload.begin(), payload.end());
    return output;
}

std::vector<uint8_t> PacketCodec::encodeAnnounce(
    const RegisteredDestination& destination,
    const Ed25519PrivateKeyBytes& signingPrivateKey,
    bool pathResponse,
    const Curve25519PublicKeyBytes* ratchetPublicKey
) const {
    auto announceRandom = buildAnnounceRandom();
    if (announceRandom.size() != ANNOUNCE_RANDOM_LENGTH) {
        return {};
    }

    std::vector<uint8_t> transcript;
    transcript.reserve(
        destination.hash.bytes.size() +
        destination.identityPublicKey.size() +
        destination.nameHash.size() +
        announceRandom.size() +
        (ratchetPublicKey != nullptr ? ratchetPublicKey->size() : 0) +
        destination.appData.size()
    );
    appendBytes(transcript, destination.hash.bytes.data(), destination.hash.bytes.size());
    appendBytes(transcript, destination.identityPublicKey.data(), destination.identityPublicKey.size());
    appendBytes(transcript, destination.nameHash.data(), destination.nameHash.size());
    appendBytes(transcript, announceRandom.data(), announceRandom.size());
    if (ratchetPublicKey != nullptr) {
        appendBytes(transcript, ratchetPublicKey->data(), ratchetPublicKey->size());
    }
    if (!destination.appData.empty()) {
        appendBytes(transcript, destination.appData.data(), destination.appData.size());
    }

    SignatureBytes signature {};
    if (!crypto::ed25519Sign(signingPrivateKey, transcript.data(), transcript.size(), signature)) {
        return {};
    }

    std::vector<uint8_t> payload;
    payload.reserve(
        destination.identityPublicKey.size() +
        destination.nameHash.size() +
        announceRandom.size() +
        (ratchetPublicKey != nullptr ? ratchetPublicKey->size() : 0) +
        signature.size() +
        destination.appData.size()
    );
    appendBytes(payload, destination.identityPublicKey.data(), destination.identityPublicKey.size());
    appendBytes(payload, destination.nameHash.data(), destination.nameHash.size());
    appendBytes(payload, announceRandom.data(), announceRandom.size());
    if (ratchetPublicKey != nullptr) {
        appendBytes(payload, ratchetPublicKey->data(), ratchetPublicKey->size());
    }
    appendBytes(payload, signature.data(), signature.size());
    if (!destination.appData.empty()) {
        appendBytes(payload, destination.appData.data(), destination.appData.size());
    }

    return encodePacket(PacketHeader {
        .headerType = HeaderType::Header1,
        .transportType = TransportType::Broadcast,
        .destinationType = destination.type,
        .packetType = PacketType::Announce,
        .context = static_cast<uint8_t>(pathResponse ? PacketContext::PathResponse : PacketContext::None),
        .contextFlag = ratchetPublicKey != nullptr,
        .hops = 0,
        .destination = destination.hash
    }, payload);
}

std::vector<uint8_t> PacketCodec::encodePathRequest(
    const DestinationHash& requestedDestination,
    const std::vector<uint8_t>& tag,
    const std::optional<DestinationHash>& requesterTransportId
) const {
    if (requestedDestination.empty() || tag.empty()) {
        return {};
    }

    std::vector<uint8_t> payload;
    payload.reserve(DESTINATION_HASH_LENGTH * (requesterTransportId.has_value() ? 2 : 1) + std::min(tag.size(), PATH_REQUEST_TAG_MAX_LENGTH));
    appendBytes(payload, requestedDestination.bytes.data(), requestedDestination.bytes.size());
    if (requesterTransportId.has_value()) {
        appendBytes(payload, requesterTransportId->bytes.data(), requesterTransportId->bytes.size());
    }
    payload.insert(payload.end(), tag.begin(), tag.begin() + std::min(tag.size(), PATH_REQUEST_TAG_MAX_LENGTH));

    return encodePacket(PacketHeader {
        .headerType = HeaderType::Header1,
        .transportType = TransportType::Broadcast,
        .destinationType = DestinationType::Plain,
        .packetType = PacketType::Data,
        .context = static_cast<uint8_t>(PacketContext::None),
        .contextFlag = false,
        .hops = 0,
        .destination = pathRequestDestinationHash()
    }, payload);
}

std::vector<uint8_t> PacketCodec::encodeLinkRequest(
    const DestinationHash& destination,
    const Curve25519PublicKeyBytes& initiatorLinkPublicKey,
    const Ed25519PublicKeyBytes& initiatorLinkSignaturePublicKey,
    LinkMode mode,
    uint32_t mtu,
    DestinationHash& outLinkId
) const {
    LinkSignalBytes signalling {};
    if (!encodeLinkSignal(mode, mtu, signalling)) {
        return {};
    }

    std::vector<uint8_t> payload;
    payload.reserve(initiatorLinkPublicKey.size() + initiatorLinkSignaturePublicKey.size() + signalling.size());
    appendBytes(payload, initiatorLinkPublicKey.data(), initiatorLinkPublicKey.size());
    appendBytes(payload, initiatorLinkSignaturePublicKey.data(), initiatorLinkSignaturePublicKey.size());
    appendBytes(payload, signalling.data(), signalling.size());

    const auto packet = encodePacket(PacketHeader {
        .headerType = HeaderType::Header1,
        .transportType = TransportType::Broadcast,
        .destinationType = DestinationType::Single,
        .packetType = PacketType::LinkRequest,
        .context = static_cast<uint8_t>(PacketContext::None),
        .contextFlag = false,
        .hops = 0,
        .destination = destination
    }, payload);

    const auto decoded = decode(packet);
    if (!decoded.has_value() || !deriveLinkId(packet, *decoded, outLinkId)) {
        return {};
    }

    return packet;
}

std::vector<uint8_t> PacketCodec::encodeLinkProof(
    const DestinationHash& linkId,
    const SignatureBytes& signature,
    const Curve25519PublicKeyBytes& responderLinkPublicKey,
    LinkMode mode,
    uint32_t mtu
) const {
    LinkSignalBytes signalling {};
    if (!encodeLinkSignal(mode, mtu, signalling)) {
        return {};
    }

    std::vector<uint8_t> payload;
    payload.reserve(signature.size() + responderLinkPublicKey.size() + signalling.size());
    appendBytes(payload, signature.data(), signature.size());
    appendBytes(payload, responderLinkPublicKey.data(), responderLinkPublicKey.size());
    appendBytes(payload, signalling.data(), signalling.size());

    return encodePacket(PacketHeader {
        .headerType = HeaderType::Header1,
        .transportType = TransportType::Broadcast,
        .destinationType = DestinationType::Link,
        .packetType = PacketType::Proof,
        .context = static_cast<uint8_t>(PacketContext::LrProof),
        .contextFlag = false,
        .hops = 0,
        .destination = linkId
    }, payload);
}

} // namespace tt::service::reticulum
