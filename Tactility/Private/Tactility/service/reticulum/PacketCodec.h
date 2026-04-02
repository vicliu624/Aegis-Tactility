#pragma once

#include <Tactility/service/reticulum/Types.h>

#include <optional>
#include <vector>

namespace tt::service::reticulum {

struct DecodedPacket {
    PacketHeader header {};
    PacketSummary summary {};
    std::vector<uint8_t> payload {};
};

class PacketCodec final {

    static bool isEnabledLinkMode(LinkMode mode);

public:

    static DestinationHash pathRequestDestinationHash();

    static bool encodeLinkSignal(LinkMode mode, uint32_t mtu, LinkSignalBytes& output);

    static bool decodeLinkSignal(const LinkSignalBytes& signalling, LinkMode& mode, uint32_t& mtu);

    std::optional<DecodedPacket> decode(const std::vector<uint8_t>& packet) const;

    std::optional<PacketSummary> summarize(const std::vector<uint8_t>& packet) const;

    std::optional<AnnounceInfo> decodeAnnounce(
        const InboundFrame& frame,
        const DecodedPacket& packet,
        const IdentityPublicKeyBytes* pinnedIdentityPublicKey = nullptr
    ) const;

    std::optional<PathRequestInfo> decodePathRequest(
        const InboundFrame& frame,
        const DecodedPacket& packet
    ) const;

    std::optional<LinkRequestInfo> decodeLinkRequest(
        const InboundFrame& frame,
        const DecodedPacket& packet,
        DestinationHash& derivedLinkId
    ) const;

    std::optional<LinkProofInfo> decodeLinkProof(
        const InboundFrame& frame,
        const DecodedPacket& packet
    ) const;

    bool deriveLinkId(const std::vector<uint8_t>& rawPacket, const DecodedPacket& packet, DestinationHash& output) const;

    std::vector<uint8_t> encodePacket(const PacketHeader& header, const std::vector<uint8_t>& payload) const;

    std::vector<uint8_t> encodeAnnounce(
        const RegisteredDestination& destination,
        const Ed25519PrivateKeyBytes& signingPrivateKey,
        bool pathResponse = false,
        const Curve25519PublicKeyBytes* ratchetPublicKey = nullptr
    ) const;

    std::vector<uint8_t> encodePathRequest(
        const DestinationHash& requestedDestination,
        const std::vector<uint8_t>& tag,
        const std::optional<DestinationHash>& requesterTransportId = {}
    ) const;

    std::vector<uint8_t> encodeLinkRequest(
        const DestinationHash& destination,
        const Curve25519PublicKeyBytes& initiatorLinkPublicKey,
        const Ed25519PublicKeyBytes& initiatorLinkSignaturePublicKey,
        LinkMode mode,
        uint32_t mtu,
        DestinationHash& outLinkId
    ) const;

    std::vector<uint8_t> encodeLinkProof(
        const DestinationHash& linkId,
        const SignatureBytes& signature,
        const Curve25519PublicKeyBytes& responderLinkPublicKey,
        LinkMode mode,
        uint32_t mtu
    ) const;
};

} // namespace tt::service::reticulum
