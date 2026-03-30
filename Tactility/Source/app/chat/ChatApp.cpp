#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && !defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include <Tactility/app/chat/ChatAppPrivate.h>
#include <Tactility/app/chat/Localization.h>
#include <Tactility/app/chat/ChatProtocol.h>

#include <Tactility/app/AppManifest.h>
#include <Tactility/Logger.h>
#include <Tactility/lvgl/LvglSync.h>

#include <algorithm>
#include <tactility/lvgl_icon_shared.h>
#include <vector>

namespace tt::app::chat {

static const auto LOGGER = Logger("ChatApp");
static constexpr auto* CHAT_ENDPOINT = "aegis.chat.broadcast";
static constexpr auto* CHAT_DESTINATION = "aegis.chat";

void ChatApp::ensureReticulumBindings() {
    const auto endpoints = service::reticulum::getAppEndpoints();
    const auto hasEndpoint = std::ranges::any_of(endpoints, [](const auto& endpoint) {
        return endpoint.name == CHAT_ENDPOINT;
    });
    if (!hasEndpoint && !service::reticulum::registerAppEndpoint(CHAT_ENDPOINT)) {
        LOGGER.warn("Failed to register chat endpoint {}", CHAT_ENDPOINT);
    }

    const auto destinations = service::reticulum::getLocalDestinations();
    const auto hasDestination = std::ranges::any_of(destinations, [](const auto& destination) {
        return destination.name == CHAT_DESTINATION;
    });
    if (!hasDestination) {
        service::reticulum::registerLocalDestination(service::reticulum::LocalDestination {
            .name = CHAT_DESTINATION,
            .acceptsLinks = false,
            .announceEnabled = true
        });
    }
}

void ChatApp::onCreate(AppContext& appContext) {
    isFirstLaunch = !settingsFileExists();
    settings = loadSettings();
    state.setLocalNickname(settings.nickname);
    if (!settings.chatChannel.empty()) {
        state.setCurrentChannel(settings.chatChannel);
    }
    ensureReticulumBindings();

    if (const auto pubsub = service::reticulum::getPubsub(); pubsub != nullptr) {
        reticulumSubscription = pubsub->subscribe([this](ReticulumEvent event) {
            onReticulumEvent(event);
        });
    }
}

void ChatApp::onDestroy(AppContext& appContext) {
    if (reticulumSubscription != nullptr) {
        if (const auto pubsub = service::reticulum::getPubsub(); pubsub != nullptr) {
            pubsub->unsubscribe(reticulumSubscription);
        }
        reticulumSubscription = nullptr;
    }
}

void ChatApp::onShow(AppContext& context, lv_obj_t* parent) {
    view.init(context, parent);
    if (isFirstLaunch) {
        view.showSettings(settings);
    }
}

void ChatApp::onReceivePayload(const std::vector<uint8_t>& data) {
    ParsedMessage parsed;
    if (!deserializeMessage(data.data(), data.size(), parsed)) {
        return;
    }

    StoredMessage msg;
    msg.displayText = parsed.senderName + ": " + parsed.message;
    msg.target = parsed.target;
    msg.isOwn = false;

    state.addMessage(msg);

    {
        auto lock = lvgl::getSyncLock()->asScopedLock();
        lock.lock();
        view.displayMessage(msg);
    }
}

void ChatApp::onReticulumEvent(const ReticulumEvent& event) {
    if (event.type != service::reticulum::EventType::AppDataReceived || !event.appData.has_value()) {
        return;
    }

    if (event.appData->endpoint != CHAT_ENDPOINT) {
        return;
    }

    onReceivePayload(event.appData->payload);
}

void ChatApp::sendMessage(const std::string& text) {
    if (text.empty()) return;

    std::string nickname = state.getLocalNickname();
    std::string channel = state.getCurrentChannel();

    std::vector<uint8_t> wireMsg;
    if (!serializeTextMessage(settings.senderId, BROADCAST_ID, nickname, channel, text, wireMsg)) {
        LOGGER.error("Failed to serialize message");
        return;
    }

    if (!service::reticulum::broadcastAppData(CHAT_ENDPOINT, wireMsg)) {
        LOGGER.error("Failed to broadcast message through Reticulum");
        return;
    }

    StoredMessage msg;
    msg.displayText = nickname + ": " + text;
    msg.target = channel;
    msg.isOwn = true;

    state.addMessage(msg);

    {
        auto lock = lvgl::getSyncLock()->asScopedLock();
        lock.lock();
        view.displayMessage(msg);
    }
}

void ChatApp::applySettings(const std::string& nickname) {
    // Trim nickname to protocol limit
    settings.nickname = nickname.substr(0, MAX_NICKNAME_LEN);

    state.setLocalNickname(settings.nickname);
    saveSettings(settings);
}

void ChatApp::switchChannel(const std::string& chatChannel) {
    const auto trimmedChannel = chatChannel.substr(0, MAX_TARGET_LEN);
    state.setCurrentChannel(trimmedChannel);
    settings.chatChannel = trimmedChannel;
    saveSettings(settings);

    {
        auto lock = lvgl::getSyncLock()->asScopedLock();
        lock.lock();
        view.refreshMessageList();
    }
}

extern const AppManifest manifest = {
    .appId = "Chat",
    .appName = "Chat",
    .resolveLocalizedAppName = &getLocalizedAppName,
    .appIcon = LVGL_ICON_SHARED_FORUM,
    .createApp = create<ChatApp>
};

} // namespace tt::app::chat

#endif // CONFIG_SOC_WIFI_SUPPORTED && !CONFIG_SLAVE_SOC_WIFI_SUPPORTED
