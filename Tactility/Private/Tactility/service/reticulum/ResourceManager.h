#pragma once

#include <Tactility/RecursiveMutex.h>
#include <Tactility/service/reticulum/Types.h>

#include <functional>
#include <optional>
#include <vector>

namespace tt::service::reticulum {

class ResourceManager final {

public:

    struct Part {
        std::vector<uint8_t> payload {};
        std::array<uint8_t, 4> mapHash {};
    };

    struct ResourceAction {
        PacketType packetType = PacketType::Data;
        uint8_t context = static_cast<uint8_t>(PacketContext::None);
        std::vector<uint8_t> payload {};
    };

    struct CompletedResource {
        ResourceInfo info {};
        std::vector<uint8_t> payload {};
    };

    using EncryptCallback = std::function<bool(
        const DestinationHash& linkId,
        const std::vector<uint8_t>& plaintext,
        std::vector<uint8_t>& ciphertext
    )>;

    using DecryptCallback = std::function<bool(
        const DestinationHash& linkId,
        const std::vector<uint8_t>& ciphertext,
        std::vector<uint8_t>& plaintext
    )>;

private:

    struct OutgoingRecord {
        ResourceInfo info {};
        std::array<uint8_t, 4> randomHash {};
        FullHashBytes expectedProof {};
        std::vector<Part> parts {};
    };

    struct IncomingRecord {
        ResourceInfo info {};
        std::array<uint8_t, 4> randomHash {};
        std::vector<std::optional<std::array<uint8_t, 4>>> partHashes {};
        std::vector<std::vector<uint8_t>> parts {};
        size_t knownHashCount = 0;
        bool waitingForHashmap = false;
    };

    mutable RecursiveMutex mutex;
    std::vector<OutgoingRecord> outgoing {};
    std::vector<IncomingRecord> incoming {};

    static std::optional<std::vector<uint8_t>> encodeAdvertisement(
        const OutgoingRecord& record,
        size_t hashSegmentIndex
    );

    static std::optional<ResourceAction> buildPartRequest(const IncomingRecord& record);

public:

    void clear();

    std::optional<ResourceInfo> startOutgoing(
        const DestinationHash& linkId,
        const std::vector<uint8_t>& payload,
        const EncryptCallback& encryptCallback,
        std::vector<ResourceAction>& outActions,
        const std::optional<DestinationHash>& requestId = {},
        bool carriesRequest = false,
        bool carriesResponse = false
    );

    std::vector<ResourceAction> acceptAdvertisement(
        const DestinationHash& linkId,
        const std::vector<uint8_t>& plaintext
    );

    std::vector<ResourceAction> handleRequest(
        const DestinationHash& linkId,
        const std::vector<uint8_t>& plaintext
    );

    std::vector<ResourceAction> handleHashmapUpdate(
        const DestinationHash& linkId,
        const std::vector<uint8_t>& plaintext
    );

    std::optional<CompletedResource> handlePart(
        const DestinationHash& linkId,
        const std::vector<uint8_t>& payload,
        const DecryptCallback& decryptCallback,
        std::vector<ResourceAction>& outActions
    );

    bool handleProof(
        const DestinationHash& linkId,
        const std::vector<uint8_t>& payload,
        ResourceInfo* updatedInfo = nullptr
    );

    bool cancelIncoming(const DestinationHash& linkId, const ResourceHashBytes& resourceHash, ResourceInfo* updatedInfo = nullptr);

    bool rejectOutgoing(const DestinationHash& linkId, const ResourceHashBytes& resourceHash, ResourceInfo* updatedInfo = nullptr);

    std::vector<ResourceInfo> getResources() const;
};

} // namespace tt::service::reticulum
