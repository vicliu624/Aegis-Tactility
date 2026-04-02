#pragma once

#include <Tactility/RecursiveMutex.h>
#include <Tactility/service/reticulum/IdentityStore.h>
#include <Tactility/service/reticulum/PacketCodec.h>
#include <Tactility/service/reticulum/Types.h>

#include <optional>
#include <vector>

namespace tt::service::reticulum {

struct LinkDataInfo {
    DestinationHash linkId {};
    uint8_t context = static_cast<uint8_t>(PacketContext::None);
    std::vector<uint8_t> plaintext {};
    LinkInfo link {};
};

class LinkManager final {

    struct Record {
        LinkInfo info {};
        DestinationHash localDestination {};
        IdentityPublicKeyBytes peerIdentityPublicKey {};
        bool peerIdentityKnown = false;
        Curve25519PrivateKeyBytes localLinkPrivateKey {};
        Curve25519PublicKeyBytes localLinkPublicKey {};
        Ed25519PrivateKeyBytes localLinkSignaturePrivateKey {};
        Ed25519PublicKeyBytes localLinkSignaturePublicKey {};
        Ed25519PrivateKeyBytes localIdentitySigningPrivateKey {};
        bool useIdentitySigningForProof = false;
        Curve25519PublicKeyBytes peerLinkPublicKey {};
        Ed25519PublicKeyBytes peerLinkSignaturePublicKey {};
        std::array<uint8_t, 64> derivedKey {};
        uint32_t requestTick = 0;
    };

    mutable RecursiveMutex mutex;
    std::vector<Record> links {};

    static bool isPlaintextContext(uint8_t context);

public:

    void clear();

    std::vector<LinkInfo> getLinks() const;

    std::optional<LinkInfo> getLink(const DestinationHash& linkId) const;

    std::optional<DestinationHash> getLocalDestination(const DestinationHash& linkId) const;

    bool beginInitiatorLink(
        const DestinationHash& peerDestination,
        const DestinationHash& peerIdentityHash,
        const IdentityPublicKeyBytes& peerIdentityPublicKey,
        const std::string& interfaceId,
        const PacketCodec& codec,
        DestinationHash& outLinkId,
        std::vector<uint8_t>& outPacket
    );

    std::optional<std::vector<uint8_t>> acceptLinkRequest(
        const LinkRequestInfo& request,
        const RegisteredDestination& localDestination,
        const IdentityStore::LocalIdentity& localIdentity,
        const PacketCodec& codec
    );

    std::optional<std::vector<uint8_t>> acceptLinkProof(
        const LinkProofInfo& proof,
        const PacketCodec& codec
    );

    std::optional<LinkDataInfo> decodeLinkData(
        const DecodedPacket& packet
    );

    bool encodeLinkData(
        const DestinationHash& linkId,
        uint8_t context,
        const std::vector<uint8_t>& plaintext,
        const PacketCodec& codec,
        std::vector<uint8_t>& outPacket
    );

    bool encryptResourceData(
        const DestinationHash& linkId,
        const std::vector<uint8_t>& plaintext,
        std::vector<uint8_t>& ciphertext
    );

    bool decryptResourceData(
        const DestinationHash& linkId,
        const std::vector<uint8_t>& ciphertext,
        std::vector<uint8_t>& plaintext
    );

    bool signLinkProof(
        const DestinationHash& linkId,
        const FullHashBytes& packetHash,
        SignatureBytes& signature
    );

    bool validateLinkProof(
        const DestinationHash& linkId,
        const FullHashBytes& packetHash,
        const SignatureBytes& signature
    );

    bool removeLink(const DestinationHash& linkId);

    bool closeLink(const DestinationHash& linkId);
};

} // namespace tt::service::reticulum
