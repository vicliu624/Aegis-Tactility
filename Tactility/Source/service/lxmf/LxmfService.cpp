#include <Tactility/service/lxmf/Lxmf.h>

#include <Tactility/Logger.h>
#include <Tactility/file/File.h>
#include <Tactility/kernel/Kernel.h>
#include <Tactility/service/ServiceManifest.h>
#include <Tactility/service/reticulum/Reticulum.h>
#include <Tactility/service/lxmf/LxmfService.h>

#include <cJSON.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <string_view>

namespace tt::service::lxmf {

namespace {

constexpr auto* STATE_FILE_NAME = "state.json";
constexpr auto* CONVERSATIONS_DIR = "conversations";
constexpr int STATE_VERSION = 1;
constexpr size_t PREVIEW_LENGTH = 80;
constexpr auto* LOCAL_DESTINATION_NAME = "lxmf.delivery";

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

    reticulum::registerLocalDestination(reticulum::LocalDestination {
        .name = LOCAL_DESTINATION_NAME,
        .appData = {},
        .acceptsLinks = true,
        .announceEnabled = true
    });

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

    setRuntimeState(RuntimeState::Stopped);
}

RuntimeState LxmfService::getRuntimeState() {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return runtimeState;
}

void LxmfService::onReticulumEvent(const reticulum::ReticulumEvent& event) {
    switch (event.type) {
        case reticulum::EventType::RuntimeStateChanged:
        case reticulum::EventType::InterfaceAttached:
        case reticulum::EventType::InterfaceDetached:
        case reticulum::EventType::InterfaceStarted:
        case reticulum::EventType::InterfaceStopped:
        case reticulum::EventType::AnnounceObserved:
        case reticulum::EventType::PathTableChanged:
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

    for (const auto& announce : reticulum::getAnnounces()) {
        auto& record = upsertRecord(announce.destination);
        record.local = record.local || announce.local;
        record.info.hops = announce.hops;

        const auto printableName = safePrintableString(announce.appData);
        if (!printableName.empty()) {
            record.info.title = printableName;
        }

        if (!announce.local) {
            record.info.subtitle = formatPeerSubtitle(announce);
        }
    }

    for (const auto& path : reticulum::getPaths()) {
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
            if (!conversation.summary.title.empty()) {
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
    const auto paths = reticulum::getPaths();

    auto lock = mutex.asScopedLock();
    lock.lock();

    std::vector<ConversationInfo> result;
    result.reserve(conversations.size());
    for (const auto& conversation : conversations) {
        auto summary = conversation.summary;
        summary.reachable = std::any_of(paths.begin(), paths.end(), [&summary](const auto& path) {
            return path.destination == summary.peerDestination && !path.unresponsive;
        });
        if (summary.title.empty()) {
            summary.title = shortenHash(summary.peerDestination);
        }
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
    auto lock = mutex.asScopedLock();
    lock.lock();

    if (auto* record = findByDestination(conversations, peerDestination, [](const ConversationRecord& item) -> const reticulum::DestinationHash& {
        return item.summary.peerDestination;
    });
        record != nullptr) {
        return record->messages;
    }

    return {};
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

    return true;
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
