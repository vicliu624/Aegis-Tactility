#include <Tactility/service/lxmf/Lxmf.h>

#include <Tactility/Logger.h>
#include <Tactility/file/File.h>
#include <Tactility/kernel/Kernel.h>
#include <Tactility/service/ServiceManifest.h>
#include <Tactility/service/lxmf/LxmfService.h>
#include <Tactility/service/reticulum/Crypto.h>
#include <Tactility/service/reticulum/DestinationRegistry.h>
#include <Tactility/service/reticulum/MessagePack.h>
#include <Tactility/service/reticulum/Reticulum.h>
#include <Tactility/settings/ChatSettings.h>

#include <cJSON.h>

#include <algorithm>
#include <cctype>
#include <ctime>
#include <format>
#include <set>
#include <string_view>

namespace tt::service::lxmf {

namespace {

constexpr auto* STATE_FILE_NAME = "state.json";
constexpr auto* CONVERSATIONS_DIR = "conversations";
constexpr int STATE_VERSION = 2;
constexpr size_t PREVIEW_LENGTH = 80;
constexpr auto* LOCAL_DESTINATION_NAME = "lxmf.delivery";
constexpr auto* DEFAULT_DISPLAY_NAME = "Anonymous Peer";
constexpr size_t LXMF_HEADER_SIZE =
    reticulum::DESTINATION_HASH_LENGTH * 2 +
    reticulum::SIGNATURE_LENGTH;

struct PackedLxmfMessage {
    std::vector<uint8_t> bytes {};
    std::string transportId {};
};

struct DecodedLxmfMessage {
    reticulum::DestinationHash destination {};
    reticulum::DestinationHash source {};
    reticulum::SignatureBytes signature {};
    std::string transportId {};
    double timestamp = 0.0;
    std::string title {};
    std::string body {};
    bool signatureValid = false;
};

constexpr int hexNibble(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
    if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
    return -1;
}

bool parseDestinationHex(const std::string& text, reticulum::DestinationHash& out) {
    if (text.size() != reticulum::DESTINATION_HASH_LENGTH * 2) {
        return false;
    }

    for (size_t i = 0; i < out.bytes.size(); i++) {
        const auto high = hexNibble(text[i * 2]);
        const auto low = hexNibble(text[i * 2 + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        out.bytes[i] = static_cast<uint8_t>((high << 4) | low);
    }

    return true;
}

std::string shortenHash(const reticulum::DestinationHash& destination) {
    const auto hex = reticulum::toHex(destination);
    if (hex.size() <= 12) {
        return hex;
    }
    return hex.substr(0, 8) + ".." + hex.substr(hex.size() - 4);
}

bool usesFallbackPeerTitle(const std::string& title, const reticulum::DestinationHash& destination) {
    return title.empty() || title == shortenHash(destination);
}

std::string makePreview(std::string_view text) {
    std::string preview;
    preview.reserve(std::min(text.size(), PREVIEW_LENGTH));

    for (const auto ch : text) {
        if (preview.size() >= PREVIEW_LENGTH) {
            break;
        }
        preview.push_back(ch == '\n' || ch == '\r' ? ' ' : ch);
    }

    if (text.size() > preview.size()) {
        preview += "...";
    }

    return preview;
}

std::string safePrintableString(const std::vector<uint8_t>& bytes) {
    std::string output;
    output.reserve(bytes.size());
    for (const auto byte : bytes) {
        if (byte == 0) {
            break;
        }

        const auto ch = static_cast<char>(byte);
        if (std::isprint(static_cast<unsigned char>(ch))) {
            output.push_back(ch);
        } else {
            return {};
        }
    }

    return output;
}

bool decodePeerAppData(const std::vector<uint8_t>& appData, std::string& displayName) {
    displayName.clear();
    if (appData.empty()) {
        return false;
    }

    if ((appData[0] & 0xF0) == 0x90 || appData[0] == 0xDC) {
        reticulum::msgpack::Reader reader(appData);
        size_t count = 0;
        if (!reader.readArrayHeader(count) || count < 1) {
            return false;
        }

        const auto savedOffset = reader.getOffset();
        if (reader.readNil()) {
            return true;
        }
        reader.setOffset(savedOffset);

        return reader.readString(displayName);
    }

    displayName = safePrintableString(appData);
    return !displayName.empty();
}

bool encodePeerAppData(const std::string& displayName, std::vector<uint8_t>& appData) {
    appData.clear();
    if (!reticulum::msgpack::appendArrayHeader(appData, 2)) {
        return false;
    }

    if (displayName.empty()) {
        if (!reticulum::msgpack::appendNil(appData)) {
            return false;
        }
    } else {
        if (!reticulum::msgpack::appendString(appData, displayName)) {
            return false;
        }
    }

    return reticulum::msgpack::appendNil(appData);
}

std::string formatPeerSubtitle(const reticulum::AnnounceInfo& announce) {
    if (!announce.interfaceId.empty()) {
        return std::format("{} hops via {}", announce.hops, announce.interfaceId);
    }
    return std::format("{} hops", announce.hops);
}

std::string formatPeerSubtitle(const reticulum::PathEntry& path) {
    if (!path.interfaceId.empty()) {
        return std::format("{} hops via {}", path.hops, path.interfaceId);
    }
    return std::format("{} hops", path.hops);
}

const reticulum::AnnounceInfo* findLxmfPeerAnnounce(
    const std::vector<reticulum::AnnounceInfo>& announces,
    const reticulum::DestinationHash& destination,
    const reticulum::NameHashBytes& deliveryNameHash
) {
    const auto iterator = std::find_if(announces.begin(), announces.end(), [&](const auto& announce) {
        return !announce.local &&
            announce.destination == destination &&
            announce.nameHash == deliveryNameHash;
    });
    return iterator != announces.end() ? &(*iterator) : nullptr;
}

const reticulum::PathEntry* findPeerPath(
    const std::vector<reticulum::PathEntry>& paths,
    const reticulum::DestinationHash& destination
) {
    const auto iterator = std::find_if(paths.begin(), paths.end(), [&](const auto& path) {
        return path.destination == destination;
    });
    return iterator != paths.end() ? &(*iterator) : nullptr;
}

void enrichPeerPresentation(
    const reticulum::DestinationHash& destination,
    const reticulum::NameHashBytes& deliveryNameHash,
    const std::vector<reticulum::AnnounceInfo>& announces,
    const std::vector<reticulum::PathEntry>& paths,
    std::string& title,
    std::string& subtitle,
    bool& reachable
) {
    if (const auto* announce = findLxmfPeerAnnounce(announces, destination, deliveryNameHash); announce != nullptr) {
        std::string displayName;
        if (decodePeerAppData(announce->appData, displayName) && !displayName.empty()) {
            title = displayName;
        }

        if (subtitle.empty()) {
            subtitle = formatPeerSubtitle(*announce);
        }
    }

    if (const auto* path = findPeerPath(paths, destination); path != nullptr) {
        reachable = !path->unresponsive;
        subtitle = formatPeerSubtitle(*path);
    }

    if (title.empty()) {
        title = shortenHash(destination);
    }
}

bool parseDeliveryState(const std::string& value, DeliveryState& output) {
    if (value == "Queued") {
        output = DeliveryState::Queued;
        return true;
    }
    if (value == "Sending") {
        output = DeliveryState::Sending;
        return true;
    }
    if (value == "Delivered") {
        output = DeliveryState::Delivered;
        return true;
    }
    if (value == "Failed") {
        output = DeliveryState::Failed;
        return true;
    }
    return false;
}

bool parseMessageDirection(const std::string& value, MessageDirection& output) {
    if (value == "Incoming") {
        output = MessageDirection::Incoming;
        return true;
    }
    if (value == "Outgoing") {
        output = MessageDirection::Outgoing;
        return true;
    }
    return false;
}

template <typename Record, typename Extractor>
Record* findByDestination(std::vector<Record>& items, const reticulum::DestinationHash& destination, Extractor extractor) {
    const auto iterator = std::find_if(items.begin(), items.end(), [&](const auto& item) {
        return extractor(item) == destination;
    });
    return iterator != items.end() ? &(*iterator) : nullptr;
}

template <typename Record, typename Extractor>
const Record* findByDestination(const std::vector<Record>& items, const reticulum::DestinationHash& destination, Extractor extractor) {
    const auto iterator = std::find_if(items.begin(), items.end(), [&](const auto& item) {
        return extractor(item) == destination;
    });
    return iterator != items.end() ? &(*iterator) : nullptr;
}

bool copyRawMessagePackObject(
    const std::vector<uint8_t>& packed,
    reticulum::msgpack::Reader& reader,
    std::vector<uint8_t>& rawObject
) {
    const auto start = reader.getOffset();
    if (!reader.skip()) {
        return false;
    }

    const auto end = reader.getOffset();
    rawObject.assign(packed.begin() + start, packed.begin() + end);
    return true;
}

bool decodeMessagePackBinAsUtf8(const std::vector<uint8_t>& rawObject, std::string& output) {
    reticulum::msgpack::Reader reader(rawObject);
    std::vector<uint8_t> bytes;
    if (!reader.readBin(bytes) || !reader.atEnd()) {
        return false;
    }

    output.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return true;
}

bool decodeMessagePackDouble(const std::vector<uint8_t>& rawObject, double& output) {
    reticulum::msgpack::Reader reader(rawObject);
    return reader.readDouble(output) && reader.atEnd();
}

bool buildPackedLxmfMessage(
    const reticulum::DestinationHash& destination,
    const reticulum::DestinationHash& source,
    double timestamp,
    std::string_view title,
    std::string_view body,
    PackedLxmfMessage& output
) {
    std::vector<uint8_t> packedPayload;
    if (!reticulum::msgpack::appendArrayHeader(packedPayload, 4) ||
        !reticulum::msgpack::appendDouble(packedPayload, timestamp) ||
        !reticulum::msgpack::appendBin(packedPayload, reinterpret_cast<const uint8_t*>(title.data()), title.size()) ||
        !reticulum::msgpack::appendBin(packedPayload, reinterpret_cast<const uint8_t*>(body.data()), body.size()) ||
        !reticulum::msgpack::appendNil(packedPayload)) {
        return false;
    }

    std::vector<uint8_t> hashedPart;
    hashedPart.reserve(destination.bytes.size() + source.bytes.size() + packedPayload.size());
    hashedPart.insert(hashedPart.end(), destination.bytes.begin(), destination.bytes.end());
    hashedPart.insert(hashedPart.end(), source.bytes.begin(), source.bytes.end());
    hashedPart.insert(hashedPart.end(), packedPayload.begin(), packedPayload.end());

    reticulum::FullHashBytes messageHash {};
    if (!reticulum::crypto::sha256(hashedPart.data(), hashedPart.size(), messageHash)) {
        return false;
    }

    std::vector<uint8_t> signedPart = hashedPart;
    signedPart.insert(signedPart.end(), messageHash.begin(), messageHash.end());

    reticulum::SignatureBytes signature {};
    if (!reticulum::signLocalIdentity(signedPart, signature)) {
        return false;
    }

    output.bytes.clear();
    output.bytes.reserve(destination.bytes.size() + source.bytes.size() + signature.size() + packedPayload.size());
    output.bytes.insert(output.bytes.end(), destination.bytes.begin(), destination.bytes.end());
    output.bytes.insert(output.bytes.end(), source.bytes.begin(), source.bytes.end());
    output.bytes.insert(output.bytes.end(), signature.begin(), signature.end());
    output.bytes.insert(output.bytes.end(), packedPayload.begin(), packedPayload.end());
    output.transportId = reticulum::toHex(messageHash);
    return true;
}

std::optional<DecodedLxmfMessage> decodePackedLxmfMessage(const std::vector<uint8_t>& lxmfBytes) {
    if (lxmfBytes.size() < LXMF_HEADER_SIZE) {
        return std::nullopt;
    }

    DecodedLxmfMessage output;
    std::copy_n(lxmfBytes.begin(), output.destination.bytes.size(), output.destination.bytes.begin());
    std::copy_n(lxmfBytes.begin() + output.destination.bytes.size(), output.source.bytes.size(), output.source.bytes.begin());
    std::copy_n(lxmfBytes.begin() + output.destination.bytes.size() + output.source.bytes.size(), output.signature.size(), output.signature.begin());

    const std::vector<uint8_t> packedPayload(lxmfBytes.begin() + LXMF_HEADER_SIZE, lxmfBytes.end());
    reticulum::msgpack::Reader reader(packedPayload);
    size_t count = 0;
    if (!reader.readArrayHeader(count) || count < 4) {
        return std::nullopt;
    }

    std::vector<uint8_t> rawTimestamp;
    std::vector<uint8_t> rawTitle;
    std::vector<uint8_t> rawBody;
    std::vector<uint8_t> rawFields;
    if (!copyRawMessagePackObject(packedPayload, reader, rawTimestamp) ||
        !copyRawMessagePackObject(packedPayload, reader, rawTitle) ||
        !copyRawMessagePackObject(packedPayload, reader, rawBody) ||
        !copyRawMessagePackObject(packedPayload, reader, rawFields)) {
        return std::nullopt;
    }

    for (size_t index = 4; index < count; index++) {
        if (!reader.skip()) {
            return std::nullopt;
        }
    }
    if (!reader.atEnd()) {
        return std::nullopt;
    }

    if (!decodeMessagePackDouble(rawTimestamp, output.timestamp) ||
        !decodeMessagePackBinAsUtf8(rawTitle, output.title) ||
        !decodeMessagePackBinAsUtf8(rawBody, output.body)) {
        return std::nullopt;
    }

    std::vector<uint8_t> normalizedPayload;
    if (!reticulum::msgpack::appendArrayHeader(normalizedPayload, 4)) {
        return std::nullopt;
    }
    normalizedPayload.insert(normalizedPayload.end(), rawTimestamp.begin(), rawTimestamp.end());
    normalizedPayload.insert(normalizedPayload.end(), rawTitle.begin(), rawTitle.end());
    normalizedPayload.insert(normalizedPayload.end(), rawBody.begin(), rawBody.end());
    normalizedPayload.insert(normalizedPayload.end(), rawFields.begin(), rawFields.end());

    std::vector<uint8_t> hashedPart;
    hashedPart.reserve(output.destination.bytes.size() + output.source.bytes.size() + normalizedPayload.size());
    hashedPart.insert(hashedPart.end(), output.destination.bytes.begin(), output.destination.bytes.end());
    hashedPart.insert(hashedPart.end(), output.source.bytes.begin(), output.source.bytes.end());
    hashedPart.insert(hashedPart.end(), normalizedPayload.begin(), normalizedPayload.end());

    reticulum::FullHashBytes messageHash {};
    if (!reticulum::crypto::sha256(hashedPart.data(), hashedPart.size(), messageHash)) {
        return std::nullopt;
    }

    std::vector<uint8_t> signedPart = hashedPart;
    signedPart.insert(signedPart.end(), messageHash.begin(), messageHash.end());
    output.transportId = reticulum::toHex(messageHash);

    if (const auto identityPublicKey = reticulum::recallIdentityPublicKey(output.source); identityPublicKey.has_value()) {
        reticulum::Ed25519PublicKeyBytes signingPublicKey {};
        std::copy_n(identityPublicKey->begin() + reticulum::CURVE25519_KEY_LENGTH, signingPublicKey.size(), signingPublicKey.begin());
        output.signatureValid = reticulum::crypto::ed25519Verify(
            signingPublicKey,
            signedPart.data(),
            signedPart.size(),
            output.signature
        );
    }

    return output;
}

} // namespace

static const auto LOGGER = Logger("LXMF");

void LxmfService::setRuntimeState(RuntimeState newState, const char* detail) {
    {
        auto lock = mutex.asScopedLock();
        lock.lock();
        runtimeState = newState;
    }

    publishEvent(LxmfEvent {
        .type = EventType::RuntimeStateChanged,
        .runtimeState = newState,
        .detail = detail != nullptr ? detail : runtimeStateToString(newState)
    });
}

void LxmfService::publishEvent(LxmfEvent event) {
    pubsub->publish(std::move(event));
}

bool LxmfService::loadState() {
    conversations.clear();
    pendingDeliveries.clear();
    nextMessageId = 1;

    const auto stateFilePath = paths->getUserDataPath(STATE_FILE_NAME);
    if (!file::isFile(stateFilePath)) {
        return true;
    }

    const auto content = file::readString(stateFilePath);
    if (content == nullptr) {
        LOGGER.error("Failed to read {}", stateFilePath);
        return false;
    }

    auto* root = cJSON_Parse(reinterpret_cast<const char*>(content.get()));
    if (root == nullptr) {
        LOGGER.error("Failed to parse {}", stateFilePath);
        return false;
    }

    const auto* nextMessageIdJson = cJSON_GetObjectItemCaseSensitive(root, "nextMessageId");
    if (cJSON_IsNumber(nextMessageIdJson)) {
        nextMessageId = static_cast<uint64_t>(cJSON_GetNumberValue(nextMessageIdJson));
    }

    const auto* conversationsJson = cJSON_GetObjectItemCaseSensitive(root, "conversations");
    if (cJSON_IsArray(conversationsJson)) {
        const auto conversationCount = cJSON_GetArraySize(conversationsJson);
        for (int i = 0; i < conversationCount; i++) {
            const auto* conversationJson = cJSON_GetArrayItem(conversationsJson, i);
            if (!cJSON_IsObject(conversationJson)) {
                continue;
            }

            ConversationRecord record;
            std::string peerHex;
            if (const auto* peerJson = cJSON_GetObjectItemCaseSensitive(conversationJson, "peer");
                cJSON_IsString(peerJson) && cJSON_GetStringValue(peerJson) != nullptr) {
                peerHex = cJSON_GetStringValue(peerJson);
            }

            if (!parseDestinationHex(peerHex, record.summary.peerDestination)) {
                continue;
            }

            if (const auto* titleJson = cJSON_GetObjectItemCaseSensitive(conversationJson, "title");
                cJSON_IsString(titleJson) && cJSON_GetStringValue(titleJson) != nullptr) {
                record.summary.title = cJSON_GetStringValue(titleJson);
            }

            if (const auto* subtitleJson = cJSON_GetObjectItemCaseSensitive(conversationJson, "subtitle");
                cJSON_IsString(subtitleJson) && cJSON_GetStringValue(subtitleJson) != nullptr) {
                record.summary.subtitle = cJSON_GetStringValue(subtitleJson);
            }

            if (const auto* unreadJson = cJSON_GetObjectItemCaseSensitive(conversationJson, "unreadCount");
                cJSON_IsNumber(unreadJson)) {
                record.summary.unreadCount = static_cast<uint32_t>(cJSON_GetNumberValue(unreadJson));
            }

            const auto* messagesJson = cJSON_GetObjectItemCaseSensitive(conversationJson, "messages");
            if (cJSON_IsArray(messagesJson)) {
                const auto messageCount = cJSON_GetArraySize(messagesJson);
                for (int messageIndex = 0; messageIndex < messageCount; messageIndex++) {
                    const auto* messageJson = cJSON_GetArrayItem(messagesJson, messageIndex);
                    if (!cJSON_IsObject(messageJson)) {
                        continue;
                    }

                    MessageInfo message;
                    message.peerDestination = record.summary.peerDestination;

                    if (const auto* idJson = cJSON_GetObjectItemCaseSensitive(messageJson, "id");
                        cJSON_IsNumber(idJson)) {
                        message.id = static_cast<uint64_t>(cJSON_GetNumberValue(idJson));
                    }

                    if (const auto* transportIdJson = cJSON_GetObjectItemCaseSensitive(messageJson, "transportId");
                        cJSON_IsString(transportIdJson) && cJSON_GetStringValue(transportIdJson) != nullptr) {
                        message.transportId = cJSON_GetStringValue(transportIdJson);
                    }

                    if (const auto* transportTimestampJson = cJSON_GetObjectItemCaseSensitive(messageJson, "transportTimestamp");
                        cJSON_IsNumber(transportTimestampJson)) {
                        message.transportTimestamp = cJSON_GetNumberValue(transportTimestampJson);
                    }

                    if (const auto* authorJson = cJSON_GetObjectItemCaseSensitive(messageJson, "author");
                        cJSON_IsString(authorJson) && cJSON_GetStringValue(authorJson) != nullptr) {
                        message.author = cJSON_GetStringValue(authorJson);
                    }

                    if (const auto* bodyJson = cJSON_GetObjectItemCaseSensitive(messageJson, "body");
                        cJSON_IsString(bodyJson) && cJSON_GetStringValue(bodyJson) != nullptr) {
                        message.body = cJSON_GetStringValue(bodyJson);
                    }

                    if (const auto* createdJson = cJSON_GetObjectItemCaseSensitive(messageJson, "createdTick");
                        cJSON_IsNumber(createdJson)) {
                        message.createdTick = static_cast<uint32_t>(cJSON_GetNumberValue(createdJson));
                    }

                    if (const auto* readJson = cJSON_GetObjectItemCaseSensitive(messageJson, "read");
                        cJSON_IsBool(readJson)) {
                        message.read = cJSON_IsTrue(readJson);
                    }

                    if (const auto* directionJson = cJSON_GetObjectItemCaseSensitive(messageJson, "direction");
                        cJSON_IsString(directionJson) && cJSON_GetStringValue(directionJson) != nullptr) {
                        parseMessageDirection(cJSON_GetStringValue(directionJson), message.direction);
                    }

                    if (const auto* deliveryJson = cJSON_GetObjectItemCaseSensitive(messageJson, "deliveryState");
                        cJSON_IsString(deliveryJson) && cJSON_GetStringValue(deliveryJson) != nullptr) {
                        parseDeliveryState(cJSON_GetStringValue(deliveryJson), message.deliveryState);
                    }

                    if (message.direction == MessageDirection::Outgoing &&
                        message.deliveryState == DeliveryState::Sending) {
                        message.deliveryState = DeliveryState::Queued;
                    }

                    record.messages.push_back(std::move(message));
                }
            }

            if (!record.messages.empty()) {
                const auto& last = record.messages.back();
                record.summary.preview = makePreview(last.body);
                record.summary.lastActivityTick = last.createdTick;
                record.summary.lastDeliveryState = last.deliveryState;
            }

            if (record.summary.title.empty()) {
                record.summary.title = shortenHash(record.summary.peerDestination);
            }

            conversations.push_back(std::move(record));
        }
    }

    for (const auto& conversation : conversations) {
        for (const auto& message : conversation.messages) {
            if (message.direction == MessageDirection::Outgoing &&
                message.deliveryState != DeliveryState::Delivered &&
                message.deliveryState != DeliveryState::Failed) {
                pendingDeliveries.push_back(PendingDeliveryRecord {
                    .messageId = message.id,
                    .peerDestination = message.peerDestination
                });
            }
        }
    }

    cJSON_Delete(root);
    return true;
}

bool LxmfService::persistLocked() const {
    if (paths == nullptr) {
        return false;
    }

    auto* root = cJSON_CreateObject();
    if (root == nullptr) {
        return false;
    }

    cJSON_AddNumberToObject(root, "version", STATE_VERSION);
    cJSON_AddNumberToObject(root, "nextMessageId", static_cast<double>(nextMessageId));

    auto* conversationsJson = cJSON_AddArrayToObject(root, "conversations");
    if (conversationsJson == nullptr) {
        cJSON_Delete(root);
        return false;
    }

    for (const auto& conversation : conversations) {
        auto* conversationJson = cJSON_CreateObject();
        if (conversationJson == nullptr) {
            cJSON_Delete(root);
            return false;
        }

        cJSON_AddStringToObject(conversationJson, "peer", reticulum::toHex(conversation.summary.peerDestination).c_str());
        cJSON_AddStringToObject(conversationJson, "title", conversation.summary.title.c_str());
        cJSON_AddStringToObject(conversationJson, "subtitle", conversation.summary.subtitle.c_str());
        cJSON_AddNumberToObject(conversationJson, "unreadCount", conversation.summary.unreadCount);

        auto* messagesJson = cJSON_AddArrayToObject(conversationJson, "messages");
        if (messagesJson == nullptr) {
            cJSON_Delete(conversationJson);
            cJSON_Delete(root);
            return false;
        }

        for (const auto& message : conversation.messages) {
            auto* messageJson = cJSON_CreateObject();
            if (messageJson == nullptr) {
                cJSON_Delete(root);
                return false;
            }

            cJSON_AddNumberToObject(messageJson, "id", static_cast<double>(message.id));
            cJSON_AddStringToObject(messageJson, "transportId", message.transportId.c_str());
            cJSON_AddNumberToObject(messageJson, "transportTimestamp", message.transportTimestamp);
            cJSON_AddStringToObject(messageJson, "direction", messageDirectionToString(message.direction));
            cJSON_AddStringToObject(messageJson, "deliveryState", deliveryStateToString(message.deliveryState));
            cJSON_AddStringToObject(messageJson, "author", message.author.c_str());
            cJSON_AddStringToObject(messageJson, "body", message.body.c_str());
            cJSON_AddNumberToObject(messageJson, "createdTick", message.createdTick);
            cJSON_AddBoolToObject(messageJson, "read", message.read);
            cJSON_AddItemToArray(messagesJson, messageJson);
        }

        cJSON_AddItemToArray(conversationsJson, conversationJson);
    }

    char* jsonString = cJSON_PrintUnformatted(root);
    const bool success = jsonString != nullptr
        && file::writeString(paths->getUserDataPath(STATE_FILE_NAME), jsonString);

    if (jsonString != nullptr) {
        cJSON_free(jsonString);
    }
    cJSON_Delete(root);
    return success;
}

bool LxmfService::initialiseLocalDeliveryDestination() {
    if (!reticulum::DestinationRegistry::deriveNameHash(LOCAL_DESTINATION_NAME, localDeliveryNameHash)) {
        return false;
    }

    auto chatSettings = settings::chat::loadOrGetDefault();
    if (chatSettings.nickname.empty()) {
        chatSettings.nickname = DEFAULT_DISPLAY_NAME;
    }

    std::vector<uint8_t> appData;
    if (!encodePeerAppData(chatSettings.nickname, appData)) {
        return false;
    }

    const auto localDestinationsBefore = reticulum::getLocalDestinations();
    bool registeredNow = false;
    const auto existing = std::find_if(localDestinationsBefore.begin(), localDestinationsBefore.end(), [](const auto& destination) {
        return destination.name == LOCAL_DESTINATION_NAME;
    });
    if (existing == localDestinationsBefore.end()) {
        if (!reticulum::registerLocalDestination(reticulum::LocalDestination {
                .name = LOCAL_DESTINATION_NAME,
                  .appData = appData,
                  .acceptsLinks = true,
                  .announceEnabled = true
              })) {
            return false;
        }
        registeredNow = true;
    }

    const auto localDestinations = reticulum::getLocalDestinations();
    const auto iterator = std::find_if(localDestinations.begin(), localDestinations.end(), [](const auto& destination) {
        return destination.name == LOCAL_DESTINATION_NAME;
    });
    if (iterator == localDestinations.end()) {
        return false;
    }

    localDeliveryDestination = iterator->hash;
    if (!registeredNow && iterator->appData != appData) {
        if (!reticulum::updateLocalDestinationAppData(localDeliveryDestination, appData) ||
            !reticulum::announceLocalDestination(localDeliveryDestination)) {
            return false;
        }
    }

    return reticulum::registerLinkHandler(localDeliveryDestination, [this](
        const reticulum::LinkInfo& link,
        uint8_t context,
        const std::vector<uint8_t>& payload,
        bool viaResource,
        const std::optional<reticulum::ResourceInfo>& resource
    ) {
        if (getRuntimeState() != RuntimeState::Ready || localDeliveryDestination.empty()) {
            return;
        }

        if (context == static_cast<uint8_t>(reticulum::PacketContext::None)) {
            handleInboundPayload(link, payload, viaResource, resource);
        }
    });
}

bool LxmfService::onStart(ServiceContext& serviceContext) {
    LOGGER.info("Starting LXMF service");

    paths = serviceContext.getPaths();
    if (paths == nullptr) {
        setRuntimeState(RuntimeState::Faulted, "No service paths available");
        return false;
    }

    setRuntimeState(RuntimeState::Starting);

    if (!file::findOrCreateDirectory(paths->getUserDataDirectory(), 0777)
        || !file::findOrCreateDirectory(paths->getUserDataPath(CONVERSATIONS_DIR), 0777)) {
        setRuntimeState(RuntimeState::Faulted, "Failed to create LXMF directories");
        return false;
    }

    {
        auto lock = mutex.asScopedLock();
        lock.lock();
        if (!loadState()) {
            setRuntimeState(RuntimeState::Faulted, "Failed to load LXMF state");
            return false;
        }
    }

    if (const auto reticulumPubsub = reticulum::getPubsub(); reticulumPubsub != nullptr) {
        reticulumSubscription = reticulumPubsub->subscribe([this](reticulum::ReticulumEvent event) {
            onReticulumEvent(event);
        });
    }

    if (!initialiseLocalDeliveryDestination()) {
        setRuntimeState(RuntimeState::Faulted, "Failed to register LXMF delivery destination");
        return false;
    }

    processPendingDeliveries();
    setRuntimeState(RuntimeState::Ready);
    return true;
}

void LxmfService::onStop(ServiceContext& serviceContext) {
    LOGGER.info("Stopping LXMF service");
    setRuntimeState(RuntimeState::Stopping);

    if (reticulumSubscription != nullptr) {
        if (const auto reticulumPubsub = reticulum::getPubsub(); reticulumPubsub != nullptr) {
            reticulumPubsub->unsubscribe(reticulumSubscription);
        }
        reticulumSubscription = nullptr;
    }

    {
        auto lock = mutex.asScopedLock();
        lock.lock();
        persistLocked();
    }

    localDeliveryDestination = {};
    localDeliveryNameHash = {};

    setRuntimeState(RuntimeState::Stopped);
}

RuntimeState LxmfService::getRuntimeState() {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return runtimeState;
}

void LxmfService::processPathUpdate(const reticulum::PathEntry& path) {
    if (!path.unresponsive) {
        processPendingDeliveries(path.destination);
    }
}

void LxmfService::processLinkUpdate(const reticulum::LinkInfo& link) {
    if (link.peerDestination.empty()) {
        return;
    }

    if (link.state == reticulum::LinkState::Active) {
        reticulum::identifyLink(link.linkId);
        processPendingDeliveries(link.peerDestination);
        return;
    }

    if (link.state != reticulum::LinkState::Closed && link.state != reticulum::LinkState::Stale) {
        return;
    }

    std::vector<MessageInfo> changedMessages;
    std::vector<ConversationInfo> changedConversations;

    {
        auto lock = mutex.asScopedLock();
        lock.lock();

        const auto findMessage = [this](uint64_t messageId) -> MessageInfo* {
            for (auto& conversation : conversations) {
                const auto iterator = std::find_if(conversation.messages.begin(), conversation.messages.end(), [&](const auto& message) {
                    return message.id == messageId;
                });
                if (iterator != conversation.messages.end()) {
                    return &(*iterator);
                }
            }
            return nullptr;
        };

        for (auto it = pendingDeliveries.begin(); it != pendingDeliveries.end();) {
            if (it->linkId != link.linkId) {
                ++it;
                continue;
            }

            auto* message = findMessage(it->messageId);
            if (message == nullptr) {
                it = pendingDeliveries.erase(it);
                continue;
            }

            message->deliveryState = it->resourceHash.has_value()
                ? DeliveryState::Failed
                : DeliveryState::Queued;
            changedMessages.push_back(*message);

            if (auto* conversation = findByDestination(conversations, message->peerDestination, [](const auto& item) -> const reticulum::DestinationHash& {
                    return item.summary.peerDestination;
                });
                conversation != nullptr) {
                conversation->summary.lastDeliveryState = message->deliveryState;
                changedConversations.push_back(conversation->summary);
            }

            if (it->resourceHash.has_value()) {
                it = pendingDeliveries.erase(it);
            } else {
                it->linkId = {};
                ++it;
            }
        }

        if ((!changedMessages.empty() || !changedConversations.empty()) && !persistLocked()) {
            LOGGER.warn("Failed to persist LXMF state after link reset");
        }
    }

    for (const auto& message : changedMessages) {
        publishEvent(LxmfEvent {
            .type = EventType::MessageListChanged,
            .runtimeState = getRuntimeState(),
            .message = message,
            .destination = message.peerDestination,
            .detail = std::format("Reset delivery state for LXMF message {}", message.id)
        });
    }

    for (const auto& conversation : changedConversations) {
        publishEvent(LxmfEvent {
            .type = EventType::ConversationListChanged,
            .runtimeState = getRuntimeState(),
            .conversation = conversation,
            .destination = conversation.peerDestination,
            .detail = std::format("Conversation transport reset for {}", reticulum::toHex(conversation.peerDestination))
        });
    }

    processPendingDeliveries(link.peerDestination);
}

void LxmfService::processResourceUpdate(const reticulum::ResourceInfo& resource) {
    if (resource.carriesRequest || resource.carriesResponse) {
        return;
    }

    MessageInfo changedMessage;
    ConversationInfo changedConversation;
    bool publishMessage = false;
    bool publishConversation = false;

    {
        auto lock = mutex.asScopedLock();
        lock.lock();

        const auto pendingIterator = std::find_if(pendingDeliveries.begin(), pendingDeliveries.end(), [&](const auto& record) {
            return record.resourceHash.has_value() && record.resourceHash.value() == resource.resourceHash;
        });
        if (pendingIterator == pendingDeliveries.end()) {
            return;
        }

        auto* message = [&]() -> MessageInfo* {
            for (auto& conversation : conversations) {
                const auto iterator = std::find_if(conversation.messages.begin(), conversation.messages.end(), [&](const auto& item) {
                    return item.id == pendingIterator->messageId;
                });
                if (iterator != conversation.messages.end()) {
                    return &(*iterator);
                }
            }
            return nullptr;
        }();
        if (message == nullptr) {
            pendingDeliveries.erase(pendingIterator);
            return;
        }

        switch (resource.state) {
            case reticulum::ResourceState::Complete:
                message->deliveryState = DeliveryState::Delivered;
                pendingDeliveries.erase(pendingIterator);
                break;

            case reticulum::ResourceState::Failed:
            case reticulum::ResourceState::Rejected:
            case reticulum::ResourceState::Corrupt:
                message->deliveryState = DeliveryState::Failed;
                pendingDeliveries.erase(pendingIterator);
                break;

            default:
                return;
        }

        changedMessage = *message;
        publishMessage = true;

        if (auto* conversation = findByDestination(conversations, message->peerDestination, [](const auto& item) -> const reticulum::DestinationHash& {
                return item.summary.peerDestination;
            });
            conversation != nullptr) {
            conversation->summary.lastDeliveryState = message->deliveryState;
            changedConversation = conversation->summary;
            publishConversation = true;
        }

        if (!persistLocked()) {
            LOGGER.warn("Failed to persist LXMF state after resource update");
        }
    }

    if (publishMessage) {
        publishEvent(LxmfEvent {
            .type = EventType::MessageListChanged,
            .runtimeState = getRuntimeState(),
            .message = changedMessage,
            .destination = changedMessage.peerDestination,
            .detail = std::format("Updated LXMF message {} after resource state {}", changedMessage.id, reticulum::resourceStateToString(resource.state))
        });
    }

    if (publishConversation) {
        publishEvent(LxmfEvent {
            .type = EventType::ConversationListChanged,
            .runtimeState = getRuntimeState(),
            .conversation = changedConversation,
            .destination = changedConversation.peerDestination,
            .detail = std::format("Updated conversation after resource {}", reticulum::toHex(resource.resourceHash))
        });
    }
}

void LxmfService::onReticulumEvent(const reticulum::ReticulumEvent& event) {
    switch (event.type) {
        case reticulum::EventType::RuntimeStateChanged:
        case reticulum::EventType::InterfaceAttached:
        case reticulum::EventType::InterfaceDetached:
        case reticulum::EventType::InterfaceStarted:
        case reticulum::EventType::InterfaceStopped:
        case reticulum::EventType::AnnounceObserved:
        case reticulum::EventType::LocalDestinationRegistered:
            publishEvent(LxmfEvent {
                .type = EventType::PeerDirectoryChanged,
                .runtimeState = getRuntimeState(),
                .detail = event.detail
            });
            publishEvent(LxmfEvent {
                .type = EventType::ConversationListChanged,
                .runtimeState = getRuntimeState(),
                .detail = event.detail
            });
            break;

        case reticulum::EventType::PathTableChanged:
            if (event.path.has_value()) {
                processPathUpdate(*event.path);
            }
            publishEvent(LxmfEvent {
                .type = EventType::PeerDirectoryChanged,
                .runtimeState = getRuntimeState(),
                .detail = event.detail
            });
            publishEvent(LxmfEvent {
                .type = EventType::ConversationListChanged,
                .runtimeState = getRuntimeState(),
                .detail = event.detail
            });
            break;

        case reticulum::EventType::LinkTableChanged:
            if (event.link.has_value()) {
                processLinkUpdate(*event.link);
            }
            break;

        case reticulum::EventType::ResourceTableChanged:
            if (event.resource.has_value()) {
                processResourceUpdate(*event.resource);
            }
            break;

        default:
            break;
    }
}

std::vector<PeerInfo> LxmfService::getPeers() {
    struct PeerRecord {
        PeerInfo info {};
        bool local = false;
    };

    std::vector<PeerRecord> records;

    auto upsertRecord = [&records](const reticulum::DestinationHash& destination) -> PeerRecord& {
        if (auto* existing = findByDestination(records, destination, [](const PeerRecord& item) -> const reticulum::DestinationHash& {
            return item.info.destination;
        }); existing != nullptr) {
            return *existing;
        }
        records.push_back(PeerRecord {
            .info = PeerInfo {
                .destination = destination,
                .title = shortenHash(destination)
            }
        });
        return records.back();
    };

    std::set<std::string> lxmfDestinations;
    for (const auto& announce : reticulum::getAnnounces()) {
        if (announce.nameHash != localDeliveryNameHash) {
            continue;
        }

        auto& record = upsertRecord(announce.destination);
        record.local = record.local || announce.local;
        record.info.hops = announce.hops;

        std::string displayName;
        if (decodePeerAppData(announce.appData, displayName) && !displayName.empty()) {
            record.info.title = displayName;
        }

        if (!announce.local) {
            record.info.subtitle = formatPeerSubtitle(announce);
        }

        lxmfDestinations.insert(reticulum::toHex(announce.destination));
    }

    for (const auto& path : reticulum::getPaths()) {
        if (lxmfDestinations.find(reticulum::toHex(path.destination)) == lxmfDestinations.end()) {
            continue;
        }

        auto& record = upsertRecord(path.destination);
        record.info.reachable = !path.unresponsive;
        record.info.hops = path.hops;
        record.info.subtitle = formatPeerSubtitle(path);
    }

      {
          auto lock = mutex.asScopedLock();
          lock.lock();
          for (const auto& conversation : conversations) {
              auto& record = upsertRecord(conversation.summary.peerDestination);
              if (!conversation.summary.title.empty() &&
                  usesFallbackPeerTitle(record.info.title, conversation.summary.peerDestination)) {
                  record.info.title = conversation.summary.title;
              }
              if (record.info.subtitle.empty() && !conversation.summary.subtitle.empty()) {
                  record.info.subtitle = conversation.summary.subtitle;
              }
        }
    }

    std::vector<PeerInfo> result;
    result.reserve(records.size());
    for (const auto& record : records) {
        if (!record.local) {
            result.push_back(record.info);
        }
    }

    std::ranges::sort(result, [](const auto& left, const auto& right) {
        if (left.reachable != right.reachable) {
            return left.reachable > right.reachable;
        }
        return left.title < right.title;
    });

    return result;
}

std::vector<ConversationInfo> LxmfService::getConversations() {
    const auto announces = reticulum::getAnnounces();
    const auto paths = reticulum::getPaths();

    auto lock = mutex.asScopedLock();
    lock.lock();

    std::vector<ConversationInfo> result;
    result.reserve(conversations.size());
      for (const auto& conversation : conversations) {
          auto summary = conversation.summary;
          enrichPeerPresentation(
              summary.peerDestination,
              localDeliveryNameHash,
              announces,
              paths,
              summary.title,
              summary.subtitle,
              summary.reachable
          );
          result.push_back(std::move(summary));
      }

    std::ranges::sort(result, [](const auto& left, const auto& right) {
        if (left.lastActivityTick != right.lastActivityTick) {
            return left.lastActivityTick > right.lastActivityTick;
        }
        return left.title < right.title;
    });

    return result;
}

std::vector<MessageInfo> LxmfService::getMessages(const reticulum::DestinationHash& peerDestination) {
    const auto announces = reticulum::getAnnounces();
    const auto paths = reticulum::getPaths();

    auto lock = mutex.asScopedLock();
    lock.lock();

    if (auto* record = findByDestination(conversations, peerDestination, [](const ConversationRecord& item) -> const reticulum::DestinationHash& {
        return item.summary.peerDestination;
    });
        record != nullptr) {
        std::string peerTitle = record->summary.title;
        std::string subtitle = record->summary.subtitle;
        bool reachable = false;
        enrichPeerPresentation(
            peerDestination,
            localDeliveryNameHash,
            announces,
            paths,
            peerTitle,
            subtitle,
            reachable
        );

        auto messages = record->messages;
        for (auto& message : messages) {
            if (message.direction == MessageDirection::Incoming) {
                message.author = peerTitle;
            }
        }
        return messages;
    }

    return {};
}

bool LxmfService::refreshLocalPeerProfile() {
    if (localDeliveryDestination.empty()) {
        return false;
    }

    auto chatSettings = settings::chat::loadOrGetDefault();
    if (chatSettings.nickname.empty()) {
        chatSettings.nickname = DEFAULT_DISPLAY_NAME;
    }

    std::vector<uint8_t> appData;
    if (!encodePeerAppData(chatSettings.nickname, appData)) {
        return false;
    }

    if (!reticulum::updateLocalDestinationAppData(localDeliveryDestination, appData) ||
        !reticulum::announceLocalDestination(localDeliveryDestination)) {
        return false;
    }

    publishEvent(LxmfEvent {
        .type = EventType::PeerDirectoryChanged,
        .runtimeState = getRuntimeState(),
        .destination = localDeliveryDestination,
        .detail = std::format("Refreshed local LXMF profile for {}", chatSettings.nickname)
    });

    publishEvent(LxmfEvent {
        .type = EventType::ConversationListChanged,
        .runtimeState = getRuntimeState(),
        .destination = localDeliveryDestination,
        .detail = std::format("Updated local LXMF announce profile for {}", chatSettings.nickname)
    });

    return true;
}

bool LxmfService::ensureConversation(
    const reticulum::DestinationHash& peerDestination,
    const std::string& title,
    const std::string& subtitle
) {
    if (peerDestination.empty()) {
        return false;
    }

    ConversationInfo snapshot;

    {
        auto lock = mutex.asScopedLock();
        lock.lock();

        auto* record = findByDestination(conversations, peerDestination, [](const ConversationRecord& item) -> const reticulum::DestinationHash& {
            return item.summary.peerDestination;
        });
        if (record == nullptr) {
            conversations.push_back(ConversationRecord {
                .summary = ConversationInfo {
                    .peerDestination = peerDestination,
                    .title = title.empty() ? shortenHash(peerDestination) : title,
                    .subtitle = subtitle
                }
            });
            record = &conversations.back();
        } else {
            if (!title.empty()) {
                record->summary.title = title;
            }
            if (!subtitle.empty()) {
                record->summary.subtitle = subtitle;
            }
        }

        snapshot = record->summary;
        if (!persistLocked()) {
            LOGGER.warn("Failed to persist LXMF state after ensureConversation");
        }
    }

    publishEvent(LxmfEvent {
        .type = EventType::ConversationListChanged,
        .runtimeState = getRuntimeState(),
        .conversation = snapshot,
        .destination = snapshot.peerDestination,
        .detail = std::format("Conversation ready for {}", reticulum::toHex(snapshot.peerDestination))
    });

    return true;
}

void LxmfService::processPendingDeliveries(const std::optional<reticulum::DestinationHash>& peerDestination) {
    struct Candidate {
        uint64_t messageId = 0;
        reticulum::DestinationHash peerDestination {};
        std::string body {};
        double transportTimestamp = 0.0;
    };

    std::vector<Candidate> candidates;
    {
        auto lock = mutex.asScopedLock();
        lock.lock();

        auto findMessage = [this](uint64_t messageId) -> const MessageInfo* {
            for (const auto& conversation : conversations) {
                const auto iterator = std::find_if(conversation.messages.begin(), conversation.messages.end(), [&](const auto& message) {
                    return message.id == messageId;
                });
                if (iterator != conversation.messages.end()) {
                    return &(*iterator);
                }
            }
            return nullptr;
        };

        for (const auto& pending : pendingDeliveries) {
            if (peerDestination.has_value() && pending.peerDestination != *peerDestination) {
                continue;
            }

            if (pending.resourceHash.has_value()) {
                continue;
            }

            const auto* message = findMessage(pending.messageId);
            if (message == nullptr ||
                message->direction != MessageDirection::Outgoing ||
                message->deliveryState == DeliveryState::Delivered ||
                message->deliveryState == DeliveryState::Failed) {
                continue;
            }

            candidates.push_back(Candidate {
                .messageId = message->id,
                .peerDestination = message->peerDestination,
                .body = message->body,
                .transportTimestamp = message->transportTimestamp
            });
        }
    }

    if (candidates.empty() || localDeliveryDestination.empty()) {
        return;
    }

    const auto links = reticulum::getLinks();
    const auto paths = reticulum::getPaths();

    for (const auto& candidate : candidates) {
        const auto activeLink = std::find_if(links.begin(), links.end(), [&](const auto& link) {
            return link.peerDestination == candidate.peerDestination &&
                link.state == reticulum::LinkState::Active;
        });

        if (activeLink != links.end()) {
            PackedLxmfMessage packedMessage;
            const auto transportTimestamp = candidate.transportTimestamp != 0.0
                ? candidate.transportTimestamp
                : static_cast<double>(std::time(nullptr));
            if (!buildPackedLxmfMessage(
                    candidate.peerDestination,
                    localDeliveryDestination,
                    transportTimestamp,
                    {},
                    candidate.body,
                    packedMessage)) {
                continue;
            }

            reticulum::ResourceInfo resource;
            if (!reticulum::sendLinkResource(activeLink->linkId, packedMessage.bytes, &resource)) {
                continue;
            }

            MessageInfo changedMessage;
            ConversationInfo changedConversation;
            bool publishConversation = false;

            {
                auto lock = mutex.asScopedLock();
                lock.lock();

                const auto pendingIterator = std::find_if(pendingDeliveries.begin(), pendingDeliveries.end(), [&](const auto& pending) {
                    return pending.messageId == candidate.messageId;
                });
                if (pendingIterator == pendingDeliveries.end()) {
                    continue;
                }

                auto* message = [&]() -> MessageInfo* {
                    for (auto& conversation : conversations) {
                        const auto iterator = std::find_if(conversation.messages.begin(), conversation.messages.end(), [&](const auto& item) {
                            return item.id == candidate.messageId;
                        });
                        if (iterator != conversation.messages.end()) {
                            return &(*iterator);
                        }
                    }
                    return nullptr;
                }();
                if (message == nullptr) {
                    continue;
                }

                pendingIterator->linkId = activeLink->linkId;
                pendingIterator->resourceHash = resource.resourceHash;

                message->transportTimestamp = transportTimestamp;
                message->transportId = packedMessage.transportId;
                message->deliveryState = DeliveryState::Sending;
                changedMessage = *message;

                if (auto* conversation = findByDestination(conversations, message->peerDestination, [](const auto& item) -> const reticulum::DestinationHash& {
                        return item.summary.peerDestination;
                    });
                    conversation != nullptr) {
                    conversation->summary.lastDeliveryState = message->deliveryState;
                    changedConversation = conversation->summary;
                    publishConversation = true;
                }

                if (!persistLocked()) {
                    LOGGER.warn("Failed to persist LXMF state after starting resource send");
                }
            }

            publishEvent(LxmfEvent {
                .type = EventType::MessageListChanged,
                .runtimeState = getRuntimeState(),
                .message = changedMessage,
                .destination = changedMessage.peerDestination,
                .detail = std::format("Sending LXMF message {} via resource {}", changedMessage.id, reticulum::toHex(resource.resourceHash))
            });

            if (publishConversation) {
                publishEvent(LxmfEvent {
                    .type = EventType::ConversationListChanged,
                    .runtimeState = getRuntimeState(),
                    .conversation = changedConversation,
                    .destination = changedConversation.peerDestination,
                    .detail = std::format("Conversation sending to {}", reticulum::toHex(changedConversation.peerDestination))
                });
            }

            continue;
        }

        const auto pendingLink = std::find_if(links.begin(), links.end(), [&](const auto& link) {
            return link.peerDestination == candidate.peerDestination &&
                (link.state == reticulum::LinkState::Pending || link.state == reticulum::LinkState::Handshake);
        });
        if (pendingLink != links.end()) {
            auto lock = mutex.asScopedLock();
            lock.lock();

            const auto pendingIterator = std::find_if(pendingDeliveries.begin(), pendingDeliveries.end(), [&](const auto& pending) {
                return pending.messageId == candidate.messageId;
            });
            if (pendingIterator != pendingDeliveries.end()) {
                pendingIterator->linkId = pendingLink->linkId;
            }

            for (auto& conversation : conversations) {
                const auto iterator = std::find_if(conversation.messages.begin(), conversation.messages.end(), [&](const auto& message) {
                    return message.id == candidate.messageId;
                });
                if (iterator != conversation.messages.end()) {
                    iterator->deliveryState = DeliveryState::Sending;
                    break;
                }
            }
            continue;
        }

        const auto pathIterator = std::find_if(paths.begin(), paths.end(), [&](const auto& path) {
            return path.destination == candidate.peerDestination && !path.unresponsive;
        });
        if (pathIterator == paths.end()) {
            reticulum::requestPath(candidate.peerDestination);
            continue;
        }

        reticulum::DestinationHash linkId {};
        if (!reticulum::openLink(candidate.peerDestination, linkId)) {
            continue;
        }

        MessageInfo changedMessage;
        ConversationInfo changedConversation;
        bool publishMessage = false;
        bool publishConversation = false;

        {
            auto lock = mutex.asScopedLock();
            lock.lock();

            const auto pendingIterator = std::find_if(pendingDeliveries.begin(), pendingDeliveries.end(), [&](const auto& pending) {
                return pending.messageId == candidate.messageId;
            });
            if (pendingIterator != pendingDeliveries.end()) {
                pendingIterator->linkId = linkId;
            }

            for (auto& conversation : conversations) {
                const auto iterator = std::find_if(conversation.messages.begin(), conversation.messages.end(), [&](const auto& message) {
                    return message.id == candidate.messageId;
                });
                if (iterator == conversation.messages.end()) {
                    continue;
                }

                iterator->deliveryState = DeliveryState::Sending;
                changedMessage = *iterator;
                publishMessage = true;

                conversation.summary.lastDeliveryState = iterator->deliveryState;
                changedConversation = conversation.summary;
                publishConversation = true;
                break;
            }

            if (publishMessage && !persistLocked()) {
                LOGGER.warn("Failed to persist LXMF state after opening link");
            }
        }

        if (publishMessage) {
            publishEvent(LxmfEvent {
                .type = EventType::MessageListChanged,
                .runtimeState = getRuntimeState(),
                .message = changedMessage,
                .destination = changedMessage.peerDestination,
                .detail = std::format("Opened LXMF link {} for message {}", reticulum::toHex(linkId), changedMessage.id)
            });
        }

        if (publishConversation) {
            publishEvent(LxmfEvent {
                .type = EventType::ConversationListChanged,
                .runtimeState = getRuntimeState(),
                .conversation = changedConversation,
                .destination = changedConversation.peerDestination,
                .detail = std::format("Conversation waiting on link {}", reticulum::toHex(linkId))
            });
        }
    }
}

bool LxmfService::queueOutgoingMessage(
    const reticulum::DestinationHash& peerDestination,
    const std::string& author,
    const std::string& body
) {
    if (peerDestination.empty() || body.empty()) {
        return false;
    }

    MessageInfo message;
    ConversationInfo conversation;

    {
        auto lock = mutex.asScopedLock();
        lock.lock();

        auto* record = findByDestination(conversations, peerDestination, [](const ConversationRecord& item) -> const reticulum::DestinationHash& {
            return item.summary.peerDestination;
        });
        if (record == nullptr) {
            conversations.push_back(ConversationRecord {
                .summary = ConversationInfo {
                    .peerDestination = peerDestination,
                    .title = shortenHash(peerDestination)
                }
            });
            record = &conversations.back();
        }

        message = MessageInfo {
            .id = nextMessageId++,
            .peerDestination = peerDestination,
            .transportTimestamp = static_cast<double>(std::time(nullptr)),
            .direction = MessageDirection::Outgoing,
            .deliveryState = DeliveryState::Queued,
            .author = author,
            .body = body,
            .createdTick = kernel::getTicks(),
            .read = true
        };

        record->messages.push_back(message);
        record->summary.preview = makePreview(body);
        record->summary.lastActivityTick = message.createdTick;
        record->summary.lastDeliveryState = message.deliveryState;

        conversation = record->summary;

        pendingDeliveries.push_back(PendingDeliveryRecord {
            .messageId = message.id,
            .peerDestination = peerDestination
        });

        if (!persistLocked()) {
            LOGGER.warn("Failed to persist LXMF state after queueOutgoingMessage");
        }
    }

    publishEvent(LxmfEvent {
        .type = EventType::MessageListChanged,
        .runtimeState = getRuntimeState(),
        .conversation = conversation,
        .message = message,
        .destination = peerDestination,
        .detail = std::format("Queued outgoing message {} for {}", message.id, reticulum::toHex(peerDestination))
    });

    publishEvent(LxmfEvent {
        .type = EventType::ConversationListChanged,
        .runtimeState = getRuntimeState(),
        .conversation = conversation,
        .destination = peerDestination,
        .detail = std::format("Conversation updated for {}", reticulum::toHex(peerDestination))
    });

    processPendingDeliveries(peerDestination);
    return true;
}

void LxmfService::handleInboundPayload(
    const reticulum::LinkInfo& link,
    const std::vector<uint8_t>& payload,
    bool viaResource,
    const std::optional<reticulum::ResourceInfo>& resource
) {
    if (localDeliveryDestination.empty()) {
        return;
    }

    const auto decoded = decodePackedLxmfMessage(payload);
    if (!decoded.has_value() || decoded->destination != localDeliveryDestination || decoded->source == localDeliveryDestination) {
        return;
    }

    if (!decoded->signatureValid && reticulum::recallIdentityPublicKey(decoded->source).has_value()) {
        publishEvent(LxmfEvent {
            .type = EventType::Error,
            .runtimeState = getRuntimeState(),
            .destination = decoded->source,
            .detail = std::format("Rejected LXMF message {} with invalid signature", decoded->transportId)
        });
        return;
    }

    MessageInfo message;
    ConversationInfo conversation;
    bool isDuplicate = false;

    {
        auto lock = mutex.asScopedLock();
        lock.lock();

        const auto* duplicate = [&]() -> const MessageInfo* {
            if (decoded->transportId.empty()) {
                return nullptr;
            }

            if (const auto* record = findByDestination(conversations, decoded->source, [](const auto& item) -> const reticulum::DestinationHash& {
                    return item.summary.peerDestination;
                });
                record != nullptr) {
                const auto iterator = std::find_if(record->messages.begin(), record->messages.end(), [&](const auto& existing) {
                    return existing.transportId == decoded->transportId;
                });
                if (iterator != record->messages.end()) {
                    return &(*iterator);
                }
            }

            return nullptr;
        }();
        if (duplicate != nullptr) {
            isDuplicate = true;
        } else {
            auto* record = findByDestination(conversations, decoded->source, [](const auto& item) -> const reticulum::DestinationHash& {
                return item.summary.peerDestination;
            });
            if (record == nullptr) {
                conversations.push_back(ConversationRecord {
                    .summary = ConversationInfo {
                        .peerDestination = decoded->source,
                        .title = shortenHash(decoded->source)
                    }
                });
                record = &conversations.back();
            }

            std::string author = record->summary.title.empty() ? shortenHash(decoded->source) : record->summary.title;
            if (author == shortenHash(decoded->source)) {
                for (const auto& announce : reticulum::getAnnounces()) {
                    if (announce.destination != decoded->source) {
                        continue;
                    }

                    std::string displayName;
                    if (decodePeerAppData(announce.appData, displayName) && !displayName.empty()) {
                        author = displayName;
                        record->summary.title = displayName;
                    }
                    if (record->summary.subtitle.empty()) {
                        record->summary.subtitle = formatPeerSubtitle(announce);
                    }
                    break;
                }
            }

            message = MessageInfo {
                .id = nextMessageId++,
                .peerDestination = decoded->source,
                .transportId = decoded->transportId,
                .transportTimestamp = decoded->timestamp,
                .direction = MessageDirection::Incoming,
                .deliveryState = DeliveryState::Delivered,
                .author = author,
                .body = decoded->body,
                .createdTick = kernel::getTicks(),
                .read = false
            };

            record->messages.push_back(message);
            record->summary.preview = makePreview(message.body);
            record->summary.lastActivityTick = message.createdTick;
            record->summary.lastDeliveryState = message.deliveryState;
            record->summary.unreadCount += 1;
            conversation = record->summary;

            if (!persistLocked()) {
                LOGGER.warn("Failed to persist LXMF state after inbound delivery");
            }
        }
    }

    if (isDuplicate) {
        return;
    }

    publishEvent(LxmfEvent {
        .type = EventType::MessageListChanged,
        .runtimeState = getRuntimeState(),
        .conversation = conversation,
        .message = message,
        .destination = decoded->source,
        .detail = std::format(
            "Received LXMF message {} on {} via {}",
            decoded->transportId,
            reticulum::toHex(link.linkId),
            viaResource && resource.has_value() ? reticulum::toHex(resource->resourceHash) : "packet"
        )
    });

    publishEvent(LxmfEvent {
        .type = EventType::ConversationListChanged,
        .runtimeState = getRuntimeState(),
        .conversation = conversation,
        .destination = decoded->source,
        .detail = std::format("Conversation updated for {}", reticulum::toHex(decoded->source))
    });
}

bool LxmfService::markConversationRead(const reticulum::DestinationHash& peerDestination) {
    ConversationInfo conversation;
    bool changed = false;

    {
        auto lock = mutex.asScopedLock();
        lock.lock();

        auto* record = findByDestination(conversations, peerDestination, [](const ConversationRecord& item) -> const reticulum::DestinationHash& {
            return item.summary.peerDestination;
        });
        if (record == nullptr) {
            return false;
        }

        for (auto& message : record->messages) {
            if (!message.read) {
                message.read = true;
                changed = true;
            }
        }
        if (record->summary.unreadCount != 0) {
            record->summary.unreadCount = 0;
            changed = true;
        }
        conversation = record->summary;

        if (changed && !persistLocked()) {
            LOGGER.warn("Failed to persist LXMF state after markConversationRead");
        }
    }

    if (changed) {
        publishEvent(LxmfEvent {
            .type = EventType::ConversationListChanged,
            .runtimeState = getRuntimeState(),
            .conversation = conversation,
            .destination = peerDestination,
            .detail = std::format("Marked conversation {} as read", reticulum::toHex(peerDestination))
        });
    }

    return true;
}

} // namespace tt::service::lxmf
