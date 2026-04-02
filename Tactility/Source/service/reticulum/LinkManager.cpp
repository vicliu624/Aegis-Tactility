#include <Tactility/service/reticulum/LinkManager.h>

#include <Tactility/kernel/Kernel.h>
#include <Tactility/service/reticulum/Crypto.h>
#include <Tactility/service/reticulum/DestinationRegistry.h>

#include <algorithm>
#include <bit>
#include <cstring>

namespace tt::service::reticulum {

namespace {

std::vector<uint8_t> encodeMessagePackFloat64(double value) {
    std::vector<uint8_t> output(9, 0);
    output[0] = 0xCB;

    uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    for (size_t index = 0; index < 8; index++) {
        output[1 + index] = static_cast<uint8_t>((bits >> ((7 - index) * 8)) & 0xFF);
    }
    return output;
}

bool decodeMessagePackFloat64(const std::vector<uint8_t>& input, double& value) {
    if (input.size() != 9 || input[0] != 0xCB) {
        return false;
    }

    uint64_t bits = 0;
    for (size_t index = 0; index < 8; index++) {
        bits = (bits << 8) | input[1 + index];
    }
    std::memcpy(&value, &bits, sizeof(value));
    return true;
}

bool deriveLinkKey(
    const DestinationHash& linkId,
    const Curve25519PrivateKeyBytes& privateKey,
    const Curve25519PublicKeyBytes& peerPublicKey,
    std::array<uint8_t, 64>& output
) {
    std::array<uint8_t, CURVE25519_KEY_LENGTH> sharedSecret {};
    return crypto::x25519SharedSecret(privateKey, peerPublicKey, sharedSecret) &&
        crypto::hkdfSha256(
            sharedSecret.data(),
            sharedSecret.size(),
            linkId.bytes.data(),
            linkId.bytes.size(),
            nullptr,
            0,
            output.data(),
            output.size()
        );
}

} // namespace

bool LinkManager::isPlaintextContext(uint8_t context) {
    switch (static_cast<PacketContext>(context)) {
        case PacketContext::Keepalive:
        case PacketContext::LinkClose:
        case PacketContext::LrRtt:
            return true;
        default:
            return false;
    }
}

void LinkManager::clear() {
    auto lock = mutex.asScopedLock();
    lock.lock();
    links.clear();
}

std::vector<LinkInfo> LinkManager::getLinks() const {
    auto lock = mutex.asScopedLock();
    lock.lock();

    std::vector<LinkInfo> snapshot;
    snapshot.reserve(links.size());
    for (const auto& record : links) {
        snapshot.push_back(record.info);
    }
    return snapshot;
}

std::optional<LinkInfo> LinkManager::getLink(const DestinationHash& linkId) const {
    auto lock = mutex.asScopedLock();
    lock.lock();

    const auto iterator = std::find_if(links.begin(), links.end(), [&linkId](const auto& record) {
        return record.info.linkId == linkId;
    });
    if (iterator != links.end()) {
        return iterator->info;
    }
    return std::nullopt;
}

std::optional<DestinationHash> LinkManager::getLocalDestination(const DestinationHash& linkId) const {
    auto lock = mutex.asScopedLock();
    lock.lock();

    const auto iterator = std::find_if(links.begin(), links.end(), [&linkId](const auto& record) {
        return record.info.linkId == linkId;
    });
    if (iterator == links.end() || iterator->localDestination.empty()) {
        return std::nullopt;
    }

    return iterator->localDestination;
}

bool LinkManager::beginInitiatorLink(
    const DestinationHash& peerDestination,
    const DestinationHash& peerIdentityHash,
    const IdentityPublicKeyBytes& peerIdentityPublicKey,
    const std::string& interfaceId,
    const PacketCodec& codec,
    DestinationHash& outLinkId,
    std::vector<uint8_t>& outPacket
) {
    Record record;
    record.info.peerDestination = peerDestination;
    record.info.peerIdentityHash = peerIdentityHash;
    record.info.state = LinkState::Pending;
    record.info.mode = LinkMode::Aes256Cbc;
    record.info.interfaceId = interfaceId;
    record.info.negotiatedMtu = RETICULUM_MTU;
    record.info.initiator = true;
    record.info.lastActivityTick = kernel::getTicks();
    record.peerIdentityPublicKey = peerIdentityPublicKey;
    record.peerIdentityKnown = true;
    record.requestTick = kernel::getTicks();
    record.useIdentitySigningForProof = false;

    if (!crypto::generateX25519KeyPair(record.localLinkPrivateKey, record.localLinkPublicKey) ||
        !crypto::generateEd25519KeyPair(record.localLinkSignaturePrivateKey, record.localLinkSignaturePublicKey)) {
        return false;
    }

    outPacket = codec.encodeLinkRequest(
        peerDestination,
        record.localLinkPublicKey,
        record.localLinkSignaturePublicKey,
        LinkMode::Aes256Cbc,
        RETICULUM_MTU,
        outLinkId
    );
    if (outPacket.empty()) {
        return false;
    }

    record.info.linkId = outLinkId;

    auto lock = mutex.asScopedLock();
    lock.lock();
    links.push_back(record);
    return true;
}

std::optional<std::vector<uint8_t>> LinkManager::acceptLinkRequest(
    const LinkRequestInfo& request,
    const RegisteredDestination& localDestination,
    const IdentityStore::LocalIdentity& localIdentity,
    const PacketCodec& codec
) {
    Record record;
    record.info.linkId = request.linkId;
    record.info.state = LinkState::Handshake;
    record.info.mode = request.mode;
    record.info.interfaceId = request.interfaceId;
    record.info.negotiatedMtu = request.requestedMtu;
    record.info.initiator = false;
    record.info.lastActivityTick = kernel::getTicks();
    record.localDestination = localDestination.hash;
    record.peerLinkPublicKey = request.initiatorLinkPublic;
    record.peerLinkSignaturePublicKey = request.initiatorLinkSignaturePublic;
    record.useIdentitySigningForProof = true;
    record.localIdentitySigningPrivateKey = localIdentity.signingPrivateKey;

    if (!crypto::generateX25519KeyPair(record.localLinkPrivateKey, record.localLinkPublicKey) ||
        !deriveLinkKey(record.info.linkId, record.localLinkPrivateKey, request.initiatorLinkPublic, record.derivedKey)) {
        return std::nullopt;
    }

    LinkSignalBytes signalling {};
    if (!PacketCodec::encodeLinkSignal(request.mode, request.requestedMtu, signalling)) {
        return std::nullopt;
    }

    std::vector<uint8_t> transcript;
    transcript.reserve(record.info.linkId.bytes.size() + record.localLinkPublicKey.size() + localIdentity.signingPublicKey.size() + signalling.size());
    transcript.insert(transcript.end(), record.info.linkId.bytes.begin(), record.info.linkId.bytes.end());
    transcript.insert(transcript.end(), record.localLinkPublicKey.begin(), record.localLinkPublicKey.end());
    transcript.insert(transcript.end(), localIdentity.signingPublicKey.begin(), localIdentity.signingPublicKey.end());
    transcript.insert(transcript.end(), signalling.begin(), signalling.end());

    SignatureBytes signature {};
    if (!crypto::ed25519Sign(localIdentity.signingPrivateKey, transcript.data(), transcript.size(), signature)) {
        return std::nullopt;
    }

    auto packet = codec.encodeLinkProof(record.info.linkId, signature, record.localLinkPublicKey, request.mode, request.requestedMtu);
    if (packet.empty()) {
        return std::nullopt;
    }

    auto lock = mutex.asScopedLock();
    lock.lock();

    const auto iterator = std::find_if(links.begin(), links.end(), [&request](const auto& existing) {
        return existing.info.linkId == request.linkId;
    });
    if (iterator != links.end()) {
        *iterator = record;
    } else {
        links.push_back(record);
    }

    return packet;
}

std::optional<std::vector<uint8_t>> LinkManager::acceptLinkProof(
    const LinkProofInfo& proof,
    const PacketCodec& codec
) {
    uint32_t rttTicks = 0;
    {
        auto lock = mutex.asScopedLock();
        lock.lock();

        const auto iterator = std::find_if(links.begin(), links.end(), [&proof](const auto& record) {
            return record.info.linkId == proof.linkId;
        });
        if (iterator == links.end() || !iterator->info.initiator || iterator->info.state != LinkState::Pending || !iterator->peerIdentityKnown) {
            return std::nullopt;
        }

        auto& record = *iterator;
        record.peerLinkPublicKey = proof.responderLinkPublic;

        const Ed25519PublicKeyBytes peerSigningPublicKey = [] (const IdentityPublicKeyBytes& publicKey) {
            Ed25519PublicKeyBytes output {};
            std::copy_n(publicKey.begin() + CURVE25519_KEY_LENGTH, output.size(), output.begin());
            return output;
        }(record.peerIdentityPublicKey);
        record.peerLinkSignaturePublicKey = peerSigningPublicKey;

        std::vector<uint8_t> transcript;
        transcript.reserve(proof.linkId.bytes.size() + proof.responderLinkPublic.size() + peerSigningPublicKey.size() + (proof.signalling.has_value() ? proof.signalling->size() : 0));
        transcript.insert(transcript.end(), proof.linkId.bytes.begin(), proof.linkId.bytes.end());
        transcript.insert(transcript.end(), proof.responderLinkPublic.begin(), proof.responderLinkPublic.end());
        transcript.insert(transcript.end(), peerSigningPublicKey.begin(), peerSigningPublicKey.end());
        if (proof.signalling.has_value()) {
            transcript.insert(transcript.end(), proof.signalling->begin(), proof.signalling->end());
        }

        if (!crypto::ed25519Verify(peerSigningPublicKey, transcript.data(), transcript.size(), proof.signature) ||
            !deriveLinkKey(record.info.linkId, record.localLinkPrivateKey, proof.responderLinkPublic, record.derivedKey)) {
            return std::nullopt;
        }

        record.info.state = LinkState::Active;
        record.info.mode = proof.mode;
        record.info.negotiatedMtu = proof.confirmedMtu;
        record.info.lastActivityTick = kernel::getTicks();
        record.info.lastRttTick = kernel::getTicks() - record.requestTick;
        rttTicks = record.info.lastRttTick;
    }

    const auto rttSeconds = static_cast<double>(rttTicks) / static_cast<double>(kernel::getTickFrequency());
    std::vector<uint8_t> lrrttPlaintext = encodeMessagePackFloat64(rttSeconds);

    std::vector<uint8_t> packet;
    if (!encodeLinkData(proof.linkId, static_cast<uint8_t>(PacketContext::LrRtt), lrrttPlaintext, codec, packet)) {
        return std::nullopt;
    }

    return packet;
}

std::optional<LinkDataInfo> LinkManager::decodeLinkData(const DecodedPacket& packet) {
    if (packet.header.packetType != PacketType::Data || packet.header.destinationType != DestinationType::Link) {
        return std::nullopt;
    }

    auto lock = mutex.asScopedLock();
    lock.lock();

    const auto iterator = std::find_if(links.begin(), links.end(), [&packet](const auto& record) {
        return record.info.linkId == packet.header.destination;
    });
    if (iterator == links.end()) {
        return std::nullopt;
    }

    auto& record = *iterator;
    std::vector<uint8_t> plaintext;
    if (isPlaintextContext(packet.header.context)) {
        plaintext = packet.payload;
    } else if (!crypto::tokenDecrypt(record.derivedKey, packet.payload, plaintext)) {
        return std::nullopt;
    }

    record.info.lastActivityTick = kernel::getTicks();
    if (!record.info.initiator && record.info.state == LinkState::Handshake) {
        record.info.state = LinkState::Active;
    }

    if (packet.header.context == static_cast<uint8_t>(PacketContext::LrRtt)) {
        double rttSeconds = 0.0;
        if (decodeMessagePackFloat64(plaintext, rttSeconds)) {
            record.info.lastRttTick = static_cast<uint32_t>(rttSeconds * kernel::getTickFrequency());
        }
    } else if (packet.header.context == static_cast<uint8_t>(PacketContext::LinkIdentify)) {
        if (!record.info.initiator) {
            if (plaintext.size() != IDENTITY_PUBLIC_KEY_LENGTH + SIGNATURE_LENGTH) {
                return std::nullopt;
            }

            IdentityPublicKeyBytes identityPublicKey {};
            SignatureBytes signature {};
            std::copy_n(plaintext.begin(), identityPublicKey.size(), identityPublicKey.begin());
            std::copy_n(plaintext.begin() + identityPublicKey.size(), signature.size(), signature.begin());

            std::vector<uint8_t> transcript;
            transcript.reserve(record.info.linkId.bytes.size() + identityPublicKey.size());
            transcript.insert(transcript.end(), record.info.linkId.bytes.begin(), record.info.linkId.bytes.end());
            transcript.insert(transcript.end(), identityPublicKey.begin(), identityPublicKey.end());

            Ed25519PublicKeyBytes signingPublicKey {};
            std::copy_n(identityPublicKey.begin() + CURVE25519_KEY_LENGTH, signingPublicKey.size(), signingPublicKey.begin());
            if (!crypto::ed25519Verify(signingPublicKey, transcript.data(), transcript.size(), signature)) {
                return std::nullopt;
            }

            record.peerIdentityPublicKey = identityPublicKey;
            record.peerIdentityKnown = true;
            record.info.remoteIdentified = true;
            if (!DestinationRegistry::deriveIdentityHash(identityPublicKey, record.info.peerIdentityHash)) {
                return std::nullopt;
            }
        }
    } else if (packet.header.context == static_cast<uint8_t>(PacketContext::LinkClose)) {
        record.info.state = LinkState::Closed;
    }

    return LinkDataInfo {
        .linkId = record.info.linkId,
        .context = packet.header.context,
        .plaintext = std::move(plaintext),
        .link = record.info
    };
}

bool LinkManager::encodeLinkData(
    const DestinationHash& linkId,
    uint8_t context,
    const std::vector<uint8_t>& plaintext,
    const PacketCodec& codec,
    std::vector<uint8_t>& outPacket
) {
    auto lock = mutex.asScopedLock();
    lock.lock();

    const auto iterator = std::find_if(links.begin(), links.end(), [&linkId](const auto& record) {
        return record.info.linkId == linkId;
    });
    if (iterator == links.end() || iterator->info.state == LinkState::Closed) {
        return false;
    }

    auto& record = *iterator;
    std::vector<uint8_t> payload;
    if (isPlaintextContext(context)) {
        payload = plaintext;
    } else if (!crypto::tokenEncrypt(record.derivedKey, plaintext, payload)) {
        return false;
    }

    record.info.lastActivityTick = kernel::getTicks();
    outPacket = codec.encodePacket(PacketHeader {
        .headerType = HeaderType::Header1,
        .transportType = TransportType::Broadcast,
        .destinationType = DestinationType::Link,
        .packetType = PacketType::Data,
        .context = context,
        .contextFlag = false,
        .hops = 0,
        .destination = linkId
    }, payload);
    return !outPacket.empty();
}

bool LinkManager::encryptResourceData(
    const DestinationHash& linkId,
    const std::vector<uint8_t>& plaintext,
    std::vector<uint8_t>& ciphertext
) {
    auto lock = mutex.asScopedLock();
    lock.lock();

    const auto iterator = std::find_if(links.begin(), links.end(), [&linkId](const auto& record) {
        return record.info.linkId == linkId;
    });
    if (iterator == links.end() || iterator->info.state == LinkState::Closed) {
        return false;
    }

    iterator->info.lastActivityTick = kernel::getTicks();
    return crypto::tokenEncrypt(iterator->derivedKey, plaintext, ciphertext);
}

bool LinkManager::decryptResourceData(
    const DestinationHash& linkId,
    const std::vector<uint8_t>& ciphertext,
    std::vector<uint8_t>& plaintext
) {
    auto lock = mutex.asScopedLock();
    lock.lock();

    const auto iterator = std::find_if(links.begin(), links.end(), [&linkId](const auto& record) {
        return record.info.linkId == linkId;
    });
    if (iterator == links.end() || iterator->info.state == LinkState::Closed) {
        return false;
    }

    iterator->info.lastActivityTick = kernel::getTicks();
    return crypto::tokenDecrypt(iterator->derivedKey, ciphertext, plaintext);
}

bool LinkManager::signLinkProof(
    const DestinationHash& linkId,
    const FullHashBytes& packetHash,
    SignatureBytes& signature
) {
    auto lock = mutex.asScopedLock();
    lock.lock();

    const auto iterator = std::find_if(links.begin(), links.end(), [&linkId](const auto& record) {
        return record.info.linkId == linkId;
    });
    if (iterator == links.end() || iterator->info.state == LinkState::Closed) {
        return false;
    }

    const auto& record = *iterator;
    const auto& signingKey = record.useIdentitySigningForProof
        ? record.localIdentitySigningPrivateKey
        : record.localLinkSignaturePrivateKey;
    return crypto::ed25519Sign(signingKey, packetHash.data(), packetHash.size(), signature);
}

bool LinkManager::validateLinkProof(
    const DestinationHash& linkId,
    const FullHashBytes& packetHash,
    const SignatureBytes& signature
) {
    auto lock = mutex.asScopedLock();
    lock.lock();

    const auto iterator = std::find_if(links.begin(), links.end(), [&linkId](const auto& record) {
        return record.info.linkId == linkId;
    });
    if (iterator == links.end() || iterator->info.state == LinkState::Closed) {
        return false;
    }

    return crypto::ed25519Verify(iterator->peerLinkSignaturePublicKey, packetHash.data(), packetHash.size(), signature);
}

bool LinkManager::removeLink(const DestinationHash& linkId) {
    auto lock = mutex.asScopedLock();
    lock.lock();

    return std::erase_if(links, [&linkId](const auto& record) {
        return record.info.linkId == linkId;
    }) > 0;
}

bool LinkManager::closeLink(const DestinationHash& linkId) {
    auto lock = mutex.asScopedLock();
    lock.lock();

    const auto iterator = std::find_if(links.begin(), links.end(), [&linkId](const auto& record) {
        return record.info.linkId == linkId;
    });
    if (iterator == links.end()) {
        return false;
    }

    iterator->info.state = LinkState::Closed;
    return true;
}

} // namespace tt::service::reticulum
