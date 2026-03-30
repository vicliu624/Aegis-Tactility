#include <Tactility/service/reticulum/PacketCodec.h>

#include <Tactility/kernel/Kernel.h>

#include <algorithm>

namespace tt::service::reticulum {

namespace {

constexpr uint8_t FLAG_HEADER_TYPE = 0x40;
constexpr uint8_t FLAG_CONTEXT = 0x20;
constexpr uint8_t MASK_PACKET_TYPE = 0x03;
constexpr uint8_t PACKET_TYPE_ANNOUNCE = 0x01;
constexpr uint8_t CONTEXT_PATH_RESPONSE = 0x0B;
constexpr size_t HASH_LENGTH = DESTINATION_HASH_LENGTH;
constexpr size_t ANNOUNCE_IDENTITY_PUBLIC_KEY_LENGTH = 64;
constexpr size_t ANNOUNCE_NAME_HASH_LENGTH = 10;
constexpr size_t ANNOUNCE_RANDOM_LENGTH = 10;
constexpr size_t ANNOUNCE_RATCHET_LENGTH = 32;
constexpr size_t ANNOUNCE_SIGNATURE_LENGTH = 64;

} // namespace

std::optional<PacketSummary> PacketCodec::summarize(const std::vector<uint8_t>& packet) const {
    if (packet.size() < 2) {
        return std::nullopt;
    }

    PacketSummary summary;
    summary.flags = packet[0];
    summary.hops = packet[1];
    summary.rawSize = packet.size();
    const auto addressBlockSize = (packet[0] & FLAG_HEADER_TYPE) != 0 ? HASH_LENGTH * 2 : HASH_LENGTH;
    if (packet.size() >= 2 + addressBlockSize + 1) {
        summary.context = packet[2 + addressBlockSize];
        summary.payloadSize = packet.size() - (2 + addressBlockSize + 1);
    } else {
        summary.payloadSize = packet.size() > 2 ? packet.size() - 2 : 0;
    }
    return summary;
}

std::optional<AnnounceInfo> PacketCodec::extractAnnounce(const InboundFrame& frame) const {
    const auto& packet = frame.payload;
    if (packet.size() < 2 + HASH_LENGTH + 1) {
        return std::nullopt;
    }

    const auto flags = packet[0];
    if ((flags & MASK_PACKET_TYPE) != PACKET_TYPE_ANNOUNCE) {
        return std::nullopt;
    }

    const bool header2 = (flags & FLAG_HEADER_TYPE) != 0;
    const bool hasRatchet = (flags & FLAG_CONTEXT) != 0;
    const size_t destinationOffset = header2 ? 2 + HASH_LENGTH : 2;
    const size_t contextOffset = destinationOffset + HASH_LENGTH;
    const size_t payloadOffset = contextOffset + 1;

    if (packet.size() < payloadOffset) {
        return std::nullopt;
    }

    size_t requiredPayload = ANNOUNCE_IDENTITY_PUBLIC_KEY_LENGTH
        + ANNOUNCE_NAME_HASH_LENGTH
        + ANNOUNCE_RANDOM_LENGTH
        + ANNOUNCE_SIGNATURE_LENGTH;
    if (hasRatchet) {
        requiredPayload += ANNOUNCE_RATCHET_LENGTH;
    }

    if (packet.size() < payloadOffset + requiredPayload) {
        return std::nullopt;
    }

    AnnounceInfo announce;
    std::copy_n(packet.begin() + destinationOffset, HASH_LENGTH, announce.destination.bytes.begin());
    announce.interfaceId = frame.interfaceId;
    announce.interfaceKind = frame.interfaceKind;
    announce.nextHop = frame.nextHop;
    announce.hops = packet[1];
    announce.context = packet[contextOffset];
    announce.pathResponse = announce.context == CONTEXT_PATH_RESPONSE;
    announce.provisional = true;
    announce.observedTick = kernel::getTicks();

    const auto appDataOffset = payloadOffset + requiredPayload;
    if (packet.size() > appDataOffset) {
        announce.appData.assign(packet.begin() + appDataOffset, packet.end());
    }

    return announce;
}

} // namespace tt::service::reticulum
