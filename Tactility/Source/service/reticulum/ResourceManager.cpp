#include <Tactility/service/reticulum/ResourceManager.h>

#include <Tactility/kernel/Kernel.h>
#include <Tactility/service/reticulum/Crypto.h>
#include <Tactility/service/reticulum/MessagePack.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace tt::service::reticulum {

namespace {

constexpr size_t RESOURCE_RANDOM_HASH_SIZE = 4;
constexpr size_t RESOURCE_STREAM_RANDOM_SIZE = 4;
constexpr size_t RESOURCE_MAP_HASH_SIZE = 4;
constexpr size_t RESOURCE_ADV_HASHMAP_MAX = 74;
constexpr size_t RESOURCE_SDU = RETICULUM_MDU;

template <typename T>
bool readExactBin(msgpack::Reader& reader, T& output) {
    std::vector<uint8_t> bytes;
    if (!reader.readBin(bytes) || bytes.size() != output.size()) {
        return false;
    }

    std::copy_n(bytes.begin(), output.size(), output.begin());
    return true;
}

std::optional<ResourceHashBytes> calculateResourceHash(
    const std::vector<uint8_t>& payload,
    const std::array<uint8_t, RESOURCE_RANDOM_HASH_SIZE>& randomHash
) {
    std::vector<uint8_t> material;
    material.reserve(payload.size() + randomHash.size());
    material.insert(material.end(), payload.begin(), payload.end());
    material.insert(material.end(), randomHash.begin(), randomHash.end());

    ResourceHashBytes hash {};
    if (!crypto::sha256(material.data(), material.size(), hash)) {
        return std::nullopt;
    }

    return hash;
}

std::optional<FullHashBytes> calculateProofHash(
    const std::vector<uint8_t>& payload,
    const ResourceHashBytes& resourceHash
) {
    std::vector<uint8_t> material;
    material.reserve(payload.size() + resourceHash.size());
    material.insert(material.end(), payload.begin(), payload.end());
    material.insert(material.end(), resourceHash.begin(), resourceHash.end());

    FullHashBytes hash {};
    if (!crypto::sha256(material.data(), material.size(), hash)) {
        return std::nullopt;
    }

    return hash;
}

std::optional<std::array<uint8_t, RESOURCE_MAP_HASH_SIZE>> calculateMapHash(
    const std::vector<uint8_t>& payload,
    const std::array<uint8_t, RESOURCE_RANDOM_HASH_SIZE>& randomHash
) {
    std::vector<uint8_t> material;
    material.reserve(payload.size() + randomHash.size());
    material.insert(material.end(), payload.begin(), payload.end());
    material.insert(material.end(), randomHash.begin(), randomHash.end());

    FullHashBytes fullHash {};
    if (!crypto::sha256(material.data(), material.size(), fullHash)) {
        return std::nullopt;
    }

    std::array<uint8_t, RESOURCE_MAP_HASH_SIZE> mapHash {};
    std::copy_n(fullHash.begin(), mapHash.size(), mapHash.begin());
    return mapHash;
}

std::vector<uint8_t> encodeRequestId(const std::optional<DestinationHash>& requestId) {
    if (!requestId.has_value()) {
        return {};
    }

    return std::vector<uint8_t>(requestId->bytes.begin(), requestId->bytes.end());
}

bool decodeRequestId(msgpack::Reader& reader, std::optional<DestinationHash>& requestId) {
    const auto savedOffset = reader.getOffset();
    if (reader.readNil()) {
        requestId.reset();
        return true;
    }
    reader.setOffset(savedOffset);

    std::vector<uint8_t> requestIdBytes;
    if (!reader.readBin(requestIdBytes) || requestIdBytes.size() != DESTINATION_HASH_LENGTH) {
        return false;
    }

    DestinationHash parsed {};
    std::copy_n(requestIdBytes.begin(), parsed.bytes.size(), parsed.bytes.begin());
    requestId = parsed;
    return true;
}

bool isResourceHashEqual(const ResourceHashBytes& left, const ResourceHashBytes& right) {
    return std::equal(left.begin(), left.end(), right.begin(), right.end());
}

bool parseResourceHash(const std::vector<uint8_t>& payload, ResourceHashBytes& resourceHash) {
    if (payload.size() < resourceHash.size()) {
        return false;
    }

    std::copy_n(payload.begin(), resourceHash.size(), resourceHash.begin());
    return true;
}

std::vector<uint8_t> buildHashmapBytes(const std::vector<ResourceManager::Part>& parts, size_t startIndex) {
    std::vector<uint8_t> hashmap;
    const auto endIndex = std::min(parts.size(), startIndex + RESOURCE_ADV_HASHMAP_MAX);
    hashmap.reserve((endIndex - startIndex) * RESOURCE_MAP_HASH_SIZE);

    for (size_t index = startIndex; index < endIndex; index++) {
        hashmap.insert(hashmap.end(), parts[index].mapHash.begin(), parts[index].mapHash.end());
    }

    return hashmap;
}

} // namespace

std::optional<ResourceManager::ResourceAction> ResourceManager::buildPartRequest(const IncomingRecord& record) {
    if (record.knownHashCount == 0) {
        return std::nullopt;
    }

    std::vector<uint8_t> requestedHashes;
    requestedHashes.reserve(record.knownHashCount * RESOURCE_MAP_HASH_SIZE);

    for (size_t index = 0; index < record.knownHashCount && index < record.partHashes.size(); index++) {
        if (!record.partHashes[index].has_value() || !record.parts[index].empty()) {
            continue;
        }

        requestedHashes.insert(
            requestedHashes.end(),
            record.partHashes[index]->begin(),
            record.partHashes[index]->end()
        );
    }

    const bool wantsMoreHashmap = record.knownHashCount < record.partHashes.size();
    std::vector<uint8_t> payload;
    payload.reserve(
        1 +
        (wantsMoreHashmap ? RESOURCE_MAP_HASH_SIZE : 0) +
        record.info.resourceHash.size() +
        requestedHashes.size()
    );

    payload.push_back(wantsMoreHashmap ? 0xFF : 0x00);
    if (wantsMoreHashmap) {
        const auto& lastKnownHash = record.partHashes[record.knownHashCount - 1];
        if (!lastKnownHash.has_value()) {
            return std::nullopt;
        }
        payload.insert(payload.end(), lastKnownHash->begin(), lastKnownHash->end());
    }

    payload.insert(payload.end(), record.info.resourceHash.begin(), record.info.resourceHash.end());
    payload.insert(payload.end(), requestedHashes.begin(), requestedHashes.end());

    return ResourceAction {
        .packetType = PacketType::Data,
        .context = static_cast<uint8_t>(PacketContext::ResourceReq),
        .payload = std::move(payload)
    };
}

std::optional<std::vector<uint8_t>> ResourceManager::encodeAdvertisement(
    const OutgoingRecord& record,
    size_t hashSegmentIndex
) {
    std::vector<uint8_t> output;
    if (!msgpack::appendMapHeader(output, 10)) {
        return std::nullopt;
    }

    const auto appendKey = [&output](const char* key) {
        return msgpack::appendString(output, key);
    };

    const auto hashmapStart = hashSegmentIndex * RESOURCE_ADV_HASHMAP_MAX;
    const auto hashmapBytes = buildHashmapBytes(record.parts, hashmapStart);

    if (!appendKey("t") || !msgpack::appendUint(output, record.info.transferSize)) return std::nullopt;
    if (!appendKey("d") || !msgpack::appendUint(output, record.info.totalSize)) return std::nullopt;
    if (!appendKey("n") || !msgpack::appendUint(output, record.info.totalParts)) return std::nullopt;
    if (!appendKey("h") || !msgpack::appendBin(output, record.info.resourceHash.data(), record.info.resourceHash.size())) return std::nullopt;
    if (!appendKey("r") || !msgpack::appendBin(output, record.randomHash.data(), record.randomHash.size())) return std::nullopt;
    if (!appendKey("o") || !msgpack::appendBin(output, record.info.resourceHash.data(), record.info.resourceHash.size())) return std::nullopt;
    if (!appendKey("i") || !msgpack::appendUint(output, 1)) return std::nullopt;
    if (!appendKey("l") || !msgpack::appendUint(output, 1)) return std::nullopt;

    if (!appendKey("q")) return std::nullopt;
    if (record.info.requestId.has_value()) {
        const auto requestIdBytes = encodeRequestId(record.info.requestId);
        if (!msgpack::appendBin(output, requestIdBytes)) return std::nullopt;
    } else {
        if (!msgpack::appendNil(output)) return std::nullopt;
    }

    uint8_t flags = 0x01;
    if (record.info.carriesRequest) {
        flags |= (1U << 3);
    }
    if (record.info.carriesResponse) {
        flags |= (1U << 4);
    }

    if (!appendKey("f") || !msgpack::appendUint(output, flags)) return std::nullopt;
    if (!appendKey("m") || !msgpack::appendBin(output, hashmapBytes)) return std::nullopt;

    return output;
}

void ResourceManager::clear() {
    auto lock = mutex.asScopedLock();
    lock.lock();
    outgoing.clear();
    incoming.clear();
}

std::optional<ResourceInfo> ResourceManager::startOutgoing(
    const DestinationHash& linkId,
    const std::vector<uint8_t>& payload,
    const EncryptCallback& encryptCallback,
    std::vector<ResourceAction>& outActions,
    const std::optional<DestinationHash>& requestId,
    bool carriesRequest,
    bool carriesResponse
) {
    if (linkId.empty() || encryptCallback == nullptr) {
        return std::nullopt;
    }

    std::array<uint8_t, RESOURCE_STREAM_RANDOM_SIZE> streamRandom {};
    std::array<uint8_t, RESOURCE_RANDOM_HASH_SIZE> resourceRandom {};
    if (!crypto::fillRandom(streamRandom.data(), streamRandom.size()) ||
        !crypto::fillRandom(resourceRandom.data(), resourceRandom.size())) {
        return std::nullopt;
    }

    std::vector<uint8_t> transferPlaintext;
    transferPlaintext.reserve(streamRandom.size() + payload.size());
    transferPlaintext.insert(transferPlaintext.end(), streamRandom.begin(), streamRandom.end());
    transferPlaintext.insert(transferPlaintext.end(), payload.begin(), payload.end());

    std::vector<uint8_t> encryptedPayload;
    if (!encryptCallback(linkId, transferPlaintext, encryptedPayload)) {
        return std::nullopt;
    }

    const auto resourceHash = calculateResourceHash(payload, resourceRandom);
    const auto proofHash = resourceHash.has_value() ? calculateProofHash(payload, *resourceHash) : std::nullopt;
    if (!resourceHash.has_value() || !proofHash.has_value()) {
        return std::nullopt;
    }

    OutgoingRecord record;
    record.info.linkId = linkId;
    record.info.resourceHash = *resourceHash;
    record.info.state = ResourceState::Advertised;
    record.info.transferSize = encryptedPayload.size();
    record.info.totalSize = payload.size();
    record.info.totalParts = static_cast<uint16_t>((encryptedPayload.size() + RESOURCE_SDU - 1) / RESOURCE_SDU);
    record.info.receivedParts = 0;
    record.info.incoming = false;
    record.info.carriesRequest = carriesRequest;
    record.info.carriesResponse = carriesResponse;
    record.info.requestId = requestId;
    record.randomHash = resourceRandom;
    record.expectedProof = *proofHash;

    record.parts.reserve(record.info.totalParts);
    for (size_t offset = 0; offset < encryptedPayload.size(); offset += RESOURCE_SDU) {
        Part part;
        const auto end = std::min(offset + RESOURCE_SDU, encryptedPayload.size());
        part.payload.assign(encryptedPayload.begin() + offset, encryptedPayload.begin() + end);

        const auto mapHash = calculateMapHash(part.payload, record.randomHash);
        if (!mapHash.has_value()) {
            return std::nullopt;
        }

        part.mapHash = *mapHash;
        record.parts.push_back(std::move(part));
    }

    const auto advertisement = encodeAdvertisement(record, 0);
    if (!advertisement.has_value()) {
        return std::nullopt;
    }

    {
        auto lock = mutex.asScopedLock();
        lock.lock();

        const auto iterator = std::find_if(outgoing.begin(), outgoing.end(), [&](const auto& existing) {
            return existing.info.linkId == record.info.linkId &&
                isResourceHashEqual(existing.info.resourceHash, record.info.resourceHash);
        });

        if (iterator != outgoing.end()) {
            *iterator = record;
        } else {
            outgoing.push_back(record);
        }
    }

    outActions.push_back(ResourceAction {
        .packetType = PacketType::Data,
        .context = static_cast<uint8_t>(PacketContext::ResourceAdv),
        .payload = *advertisement
    });

    return record.info;
}

std::vector<ResourceManager::ResourceAction> ResourceManager::acceptAdvertisement(
    const DestinationHash& linkId,
    const std::vector<uint8_t>& plaintext
) {
    msgpack::Reader reader(plaintext);
    size_t mapCount = 0;
    if (!reader.readMapHeader(mapCount)) {
        return {};
    }

    uint64_t transferSize = 0;
    uint64_t totalSize = 0;
    uint64_t partCount = 0;
    ResourceHashBytes resourceHash {};
    std::array<uint8_t, RESOURCE_RANDOM_HASH_SIZE> randomHash {};
    uint64_t segmentIndex = 0;
    uint64_t segmentCount = 0;
    std::optional<DestinationHash> requestId {};
    uint64_t flags = 0;
    std::vector<uint8_t> hashmapBytes;

    bool hasTransferSize = false;
    bool hasTotalSize = false;
    bool hasPartCount = false;
    bool hasResourceHash = false;
    bool hasRandomHash = false;
    bool hasSegmentIndex = false;
    bool hasSegmentCount = false;
    bool hasFlags = false;
    bool hasHashmap = false;

    for (size_t index = 0; index < mapCount; index++) {
        std::string key;
        if (!reader.readString(key)) {
            return {};
        }

        if (key == "t") {
            hasTransferSize = reader.readUint(transferSize);
        } else if (key == "d") {
            hasTotalSize = reader.readUint(totalSize);
        } else if (key == "n") {
            hasPartCount = reader.readUint(partCount);
        } else if (key == "h") {
            hasResourceHash = readExactBin(reader, resourceHash);
        } else if (key == "r") {
            hasRandomHash = readExactBin(reader, randomHash);
        } else if (key == "o") {
            std::vector<uint8_t> ignored;
            if (!reader.readBin(ignored)) {
                return {};
            }
        } else if (key == "i") {
            hasSegmentIndex = reader.readUint(segmentIndex);
        } else if (key == "l") {
            hasSegmentCount = reader.readUint(segmentCount);
        } else if (key == "q") {
            if (!decodeRequestId(reader, requestId)) {
                return {};
            }
        } else if (key == "f") {
            hasFlags = reader.readUint(flags);
        } else if (key == "m") {
            hasHashmap = reader.readBin(hashmapBytes);
        } else {
            return {};
        }
    }

    if (!hasTransferSize || !hasTotalSize || !hasPartCount || !hasResourceHash || !hasRandomHash ||
        !hasSegmentIndex || !hasSegmentCount || !hasFlags || !hasHashmap ||
        partCount == 0 || segmentIndex == 0 || segmentCount == 0 ||
        (hashmapBytes.size() % RESOURCE_MAP_HASH_SIZE) != 0) {
        return {};
    }

    IncomingRecord record;
    record.info.linkId = linkId;
    record.info.resourceHash = resourceHash;
    record.info.state = ResourceState::Transferring;
    record.info.transferSize = transferSize;
    record.info.totalSize = totalSize;
    record.info.totalParts = static_cast<uint16_t>(partCount);
    record.info.receivedParts = 0;
    record.info.incoming = true;
    record.info.carriesRequest = ((flags >> 3) & 0x01U) != 0;
    record.info.carriesResponse = ((flags >> 4) & 0x01U) != 0;
    record.info.requestId = requestId;
    record.randomHash = randomHash;
    record.partHashes.resize(partCount);
    record.parts.resize(partCount);

    const auto hashCount = std::min(static_cast<size_t>(partCount), hashmapBytes.size() / RESOURCE_MAP_HASH_SIZE);
    for (size_t index = 0; index < hashCount; index++) {
        std::array<uint8_t, RESOURCE_MAP_HASH_SIZE> mapHash {};
        std::copy_n(hashmapBytes.begin() + index * RESOURCE_MAP_HASH_SIZE, mapHash.size(), mapHash.begin());
        record.partHashes[index] = mapHash;
    }
    record.knownHashCount = hashCount;

    {
        auto lock = mutex.asScopedLock();
        lock.lock();

        const auto iterator = std::find_if(incoming.begin(), incoming.end(), [&](const auto& existing) {
            return existing.info.linkId == linkId &&
                isResourceHashEqual(existing.info.resourceHash, resourceHash);
        });

        if (iterator != incoming.end()) {
            *iterator = record;
        } else {
            incoming.push_back(record);
        }
    }

    std::vector<ResourceAction> actions;
    if (const auto request = buildPartRequest(record); request.has_value()) {
        actions.push_back(*request);
    }
    return actions;
}

std::vector<ResourceManager::ResourceAction> ResourceManager::handleRequest(
    const DestinationHash& linkId,
    const std::vector<uint8_t>& plaintext
) {
    if (plaintext.size() < 1 + RESOURCE_HASH_LENGTH) {
        return {};
    }

    const bool wantsMoreHashmap = plaintext[0] == 0xFF;
    size_t offset = 1;
    std::array<uint8_t, RESOURCE_MAP_HASH_SIZE> lastMapHash {};
    if (wantsMoreHashmap) {
        if (plaintext.size() < 1 + RESOURCE_MAP_HASH_SIZE + RESOURCE_HASH_LENGTH) {
            return {};
        }

        std::copy_n(plaintext.begin() + offset, lastMapHash.size(), lastMapHash.begin());
        offset += lastMapHash.size();
    }

    ResourceHashBytes resourceHash {};
    std::copy_n(plaintext.begin() + offset, resourceHash.size(), resourceHash.begin());
    offset += resourceHash.size();

    std::vector<std::array<uint8_t, RESOURCE_MAP_HASH_SIZE>> requestedHashes;
    while (offset + RESOURCE_MAP_HASH_SIZE <= plaintext.size()) {
        std::array<uint8_t, RESOURCE_MAP_HASH_SIZE> mapHash {};
        std::copy_n(plaintext.begin() + offset, mapHash.size(), mapHash.begin());
        requestedHashes.push_back(mapHash);
        offset += mapHash.size();
    }

    std::vector<ResourceAction> actions;

    auto lock = mutex.asScopedLock();
    lock.lock();

    const auto iterator = std::find_if(outgoing.begin(), outgoing.end(), [&](const auto& record) {
        return record.info.linkId == linkId &&
            isResourceHashEqual(record.info.resourceHash, resourceHash);
    });
    if (iterator == outgoing.end()) {
        return {};
    }

    auto& record = *iterator;
    record.info.state = ResourceState::Transferring;

    for (const auto& requestedHash : requestedHashes) {
        const auto partIterator = std::find_if(record.parts.begin(), record.parts.end(), [&](const auto& part) {
            return part.mapHash == requestedHash;
        });
        if (partIterator != record.parts.end()) {
            actions.push_back(ResourceAction {
                .packetType = PacketType::Data,
                .context = static_cast<uint8_t>(PacketContext::Resource),
                .payload = partIterator->payload
            });
        }
    }

    if (wantsMoreHashmap) {
        const auto hashIterator = std::find_if(record.parts.begin(), record.parts.end(), [&](const auto& part) {
            return part.mapHash == lastMapHash;
        });
        if (hashIterator == record.parts.end()) {
            record.info.state = ResourceState::Failed;
            actions.push_back(ResourceAction {
                .packetType = PacketType::Data,
                .context = static_cast<uint8_t>(PacketContext::ResourceIcl),
                .payload = std::vector<uint8_t>(record.info.resourceHash.begin(), record.info.resourceHash.end())
            });
            return actions;
        }

        const auto boundary = static_cast<size_t>(std::distance(record.parts.begin(), hashIterator) + 1);
        if ((boundary % RESOURCE_ADV_HASHMAP_MAX) != 0) {
            record.info.state = ResourceState::Failed;
            actions.push_back(ResourceAction {
                .packetType = PacketType::Data,
                .context = static_cast<uint8_t>(PacketContext::ResourceIcl),
                .payload = std::vector<uint8_t>(record.info.resourceHash.begin(), record.info.resourceHash.end())
            });
            return actions;
        }

        const auto segmentNumber = boundary / RESOURCE_ADV_HASHMAP_MAX;
        const auto hashmapBytes = buildHashmapBytes(record.parts, boundary);

        std::vector<uint8_t> hmuPayload;
        hmuPayload.insert(hmuPayload.end(), record.info.resourceHash.begin(), record.info.resourceHash.end());
        if (!msgpack::appendArrayHeader(hmuPayload, 2) ||
            !msgpack::appendUint(hmuPayload, segmentNumber) ||
            !msgpack::appendBin(hmuPayload, hashmapBytes)) {
            return actions;
        }

        actions.push_back(ResourceAction {
            .packetType = PacketType::Data,
            .context = static_cast<uint8_t>(PacketContext::ResourceHmu),
            .payload = std::move(hmuPayload)
        });
    }

    if (record.info.totalParts != 0 && actions.empty()) {
        record.info.state = ResourceState::AwaitingProof;
    }

    return actions;
}

std::vector<ResourceManager::ResourceAction> ResourceManager::handleHashmapUpdate(
    const DestinationHash& linkId,
    const std::vector<uint8_t>& plaintext
) {
    if (plaintext.size() < RESOURCE_HASH_LENGTH) {
        return {};
    }

    ResourceHashBytes resourceHash {};
    std::copy_n(plaintext.begin(), resourceHash.size(), resourceHash.begin());

    std::vector<uint8_t> packed(plaintext.begin() + resourceHash.size(), plaintext.end());
    msgpack::Reader reader(packed);
    size_t itemCount = 0;
    if (!reader.readArrayHeader(itemCount) || itemCount != 2) {
        return {};
    }

    uint64_t segmentNumber = 0;
    std::vector<uint8_t> hashmapBytes;
    if (!reader.readUint(segmentNumber) || !reader.readBin(hashmapBytes) || !reader.atEnd()) {
        return {};
    }

    auto lock = mutex.asScopedLock();
    lock.lock();

    const auto iterator = std::find_if(incoming.begin(), incoming.end(), [&](const auto& record) {
        return record.info.linkId == linkId &&
            isResourceHashEqual(record.info.resourceHash, resourceHash);
    });
    if (iterator == incoming.end() || (hashmapBytes.size() % RESOURCE_MAP_HASH_SIZE) != 0) {
        return {};
    }

    auto& record = *iterator;
    const auto startIndex = static_cast<size_t>(segmentNumber) * RESOURCE_ADV_HASHMAP_MAX;
    if (startIndex >= record.partHashes.size()) {
        return {};
    }

    const auto hashCount = std::min(
        (record.partHashes.size() - startIndex),
        hashmapBytes.size() / RESOURCE_MAP_HASH_SIZE
    );
    for (size_t index = 0; index < hashCount; index++) {
        std::array<uint8_t, RESOURCE_MAP_HASH_SIZE> mapHash {};
        std::copy_n(hashmapBytes.begin() + index * RESOURCE_MAP_HASH_SIZE, mapHash.size(), mapHash.begin());
        record.partHashes[startIndex + index] = mapHash;
    }

    record.knownHashCount = std::max(record.knownHashCount, startIndex + hashCount);
    record.waitingForHashmap = false;

    std::vector<ResourceAction> actions;
    if (const auto request = buildPartRequest(record); request.has_value()) {
        actions.push_back(*request);
    }
    return actions;
}

std::optional<ResourceManager::CompletedResource> ResourceManager::handlePart(
    const DestinationHash& linkId,
    const std::vector<uint8_t>& payload,
    const DecryptCallback& decryptCallback,
    std::vector<ResourceAction>& outActions
) {
    auto lock = mutex.asScopedLock();
    lock.lock();

    for (auto iterator = incoming.begin(); iterator != incoming.end(); ++iterator) {
        auto& record = *iterator;
        if (record.info.linkId != linkId || record.info.state == ResourceState::Complete) {
            continue;
        }

        const auto mapHash = calculateMapHash(payload, record.randomHash);
        if (!mapHash.has_value()) {
            continue;
        }

        bool matched = false;
        for (size_t index = 0; index < record.knownHashCount && index < record.partHashes.size(); index++) {
            if (!record.partHashes[index].has_value() || record.parts[index].empty()) {
                if (record.partHashes[index].has_value() && record.partHashes[index].value() == *mapHash) {
                    record.parts[index] = payload;
                    record.info.receivedParts++;
                    matched = true;
                    break;
                }
            }
        }

        if (!matched) {
            continue;
        }

        const bool allKnownReceived = std::all_of(
            record.parts.begin(),
            record.parts.begin() + record.knownHashCount,
            [](const auto& part) { return !part.empty(); }
        );

        if (record.info.receivedParts == record.info.totalParts) {
            std::vector<uint8_t> encryptedPayload;
            encryptedPayload.reserve(record.info.transferSize);
            for (const auto& part : record.parts) {
                encryptedPayload.insert(encryptedPayload.end(), part.begin(), part.end());
            }

            std::vector<uint8_t> decryptedPayload;
            if (!decryptCallback(linkId, encryptedPayload, decryptedPayload) ||
                decryptedPayload.size() < RESOURCE_STREAM_RANDOM_SIZE) {
                record.info.state = ResourceState::Corrupt;
                return std::nullopt;
            }

            std::vector<uint8_t> originalPayload(
                decryptedPayload.begin() + RESOURCE_STREAM_RANDOM_SIZE,
                decryptedPayload.end()
            );

            const auto expectedHash = calculateResourceHash(originalPayload, record.randomHash);
            if (!expectedHash.has_value() || !isResourceHashEqual(*expectedHash, record.info.resourceHash)) {
                record.info.state = ResourceState::Corrupt;
                return std::nullopt;
            }

            const auto proofHash = calculateProofHash(originalPayload, record.info.resourceHash);
            if (!proofHash.has_value()) {
                record.info.state = ResourceState::Corrupt;
                return std::nullopt;
            }

            std::vector<uint8_t> proofPayload;
            proofPayload.insert(proofPayload.end(), record.info.resourceHash.begin(), record.info.resourceHash.end());
            proofPayload.insert(proofPayload.end(), proofHash->begin(), proofHash->end());
            outActions.push_back(ResourceAction {
                .packetType = PacketType::Proof,
                .context = static_cast<uint8_t>(PacketContext::ResourcePrf),
                .payload = std::move(proofPayload)
            });

            record.info.state = ResourceState::Complete;
            auto completed = CompletedResource {
                .info = record.info,
                .payload = std::move(originalPayload)
            };
            incoming.erase(iterator);
            return completed;
        }

        if (allKnownReceived && record.knownHashCount < record.partHashes.size()) {
            if (const auto request = buildPartRequest(record); request.has_value()) {
                record.waitingForHashmap = true;
                outActions.push_back(*request);
            }
        }

        return std::nullopt;
    }

    return std::nullopt;
}

bool ResourceManager::handleProof(
    const DestinationHash& linkId,
    const std::vector<uint8_t>& payload,
    ResourceInfo* updatedInfo
) {
    if (payload.size() != RESOURCE_HASH_LENGTH + FULL_HASH_LENGTH) {
        return false;
    }

    ResourceHashBytes resourceHash {};
    std::copy_n(payload.begin(), resourceHash.size(), resourceHash.begin());

    FullHashBytes proofHash {};
    std::copy_n(payload.begin() + resourceHash.size(), proofHash.size(), proofHash.begin());

    auto lock = mutex.asScopedLock();
    lock.lock();

    const auto iterator = std::find_if(outgoing.begin(), outgoing.end(), [&](const auto& record) {
        return record.info.linkId == linkId &&
            isResourceHashEqual(record.info.resourceHash, resourceHash);
    });
    if (iterator == outgoing.end()) {
        return false;
    }

    if (iterator->expectedProof != proofHash) {
        return false;
    }

    iterator->info.state = ResourceState::Complete;
    if (updatedInfo != nullptr) {
        *updatedInfo = iterator->info;
    }

    outgoing.erase(iterator);
    return true;
}

bool ResourceManager::cancelIncoming(const DestinationHash& linkId, const ResourceHashBytes& resourceHash, ResourceInfo* updatedInfo) {
    auto lock = mutex.asScopedLock();
    lock.lock();

    const auto iterator = std::find_if(incoming.begin(), incoming.end(), [&](const auto& record) {
        return record.info.linkId == linkId &&
            isResourceHashEqual(record.info.resourceHash, resourceHash);
    });
    if (iterator == incoming.end()) {
        return false;
    }

    iterator->info.state = ResourceState::Failed;
    if (updatedInfo != nullptr) {
        *updatedInfo = iterator->info;
    }

    incoming.erase(iterator);
    return true;
}

bool ResourceManager::rejectOutgoing(const DestinationHash& linkId, const ResourceHashBytes& resourceHash, ResourceInfo* updatedInfo) {
    auto lock = mutex.asScopedLock();
    lock.lock();

    const auto iterator = std::find_if(outgoing.begin(), outgoing.end(), [&](const auto& record) {
        return record.info.linkId == linkId &&
            isResourceHashEqual(record.info.resourceHash, resourceHash);
    });
    if (iterator == outgoing.end()) {
        return false;
    }

    iterator->info.state = ResourceState::Rejected;
    if (updatedInfo != nullptr) {
        *updatedInfo = iterator->info;
    }

    outgoing.erase(iterator);
    return true;
}

std::vector<ResourceInfo> ResourceManager::getResources() const {
    auto lock = mutex.asScopedLock();
    lock.lock();

    std::vector<ResourceInfo> resources;
    resources.reserve(outgoing.size() + incoming.size());

    for (const auto& record : outgoing) {
        resources.push_back(record.info);
    }
    for (const auto& record : incoming) {
        resources.push_back(record.info);
    }

    return resources;
}

} // namespace tt::service::reticulum
