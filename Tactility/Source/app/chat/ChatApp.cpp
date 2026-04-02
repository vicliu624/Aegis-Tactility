#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && !defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include <Tactility/app/chat/ChatAppPrivate.h>
#include <Tactility/app/chat/Localization.h>

#include <Tactility/app/AppManifest.h>
#include <Tactility/Logger.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/service/lxmf/Lxmf.h>
#include <Tactility/settings/ChatSettings.h>

#include <tactility/lvgl_icon_shared.h>

#include <algorithm>

namespace tt::app::chat {

namespace {

static std::string shortenHash(const service::reticulum::DestinationHash& destination) {
    const auto hex = service::reticulum::toHex(destination);
    if (hex.size() <= 12) {
        return hex;
    }
    return hex.substr(0, 8) + ".." + hex.substr(hex.size() - 4);
}

static std::string resolveConversationTitle(const service::lxmf::ConversationInfo& conversation) {
    return conversation.title.empty() ? shortenHash(conversation.peerDestination) : conversation.title;
}

static std::string resolvePeerTitle(const service::lxmf::PeerInfo& peer) {
    return peer.title.empty() ? shortenHash(peer.destination) : peer.title;
}

} // namespace

static const auto LOGGER = Logger("ChatApp");

void ChatApp::refreshStateFromServices() {
    const auto screenMode = state.getScreenMode();
    auto settingsSnapshot = settings::chat::loadOrGetDefault();
    if (settingsSnapshot.nickname.empty()) {
        settingsSnapshot.nickname = getDefaultNickname();
    }
    state.setLocalNickname(settingsSnapshot.nickname);

    const auto conversations = service::lxmf::getConversations();
    const auto peers = service::lxmf::getPeers();
    state.setConversations(conversations);
    state.setPeers(peers);

    if (screenMode == ScreenMode::Thread) {
        service::reticulum::DestinationHash peerDestination;
        if (state.getActivePeer(peerDestination)) {
            const auto conversation = std::find_if(conversations.begin(), conversations.end(), [&](const auto& item) {
                return item.peerDestination == peerDestination;
            });
            if (conversation != conversations.end()) {
                state.setActiveTitle(resolveConversationTitle(*conversation));
            } else {
                const auto peer = std::find_if(peers.begin(), peers.end(), [&](const auto& item) {
                    return item.destination == peerDestination;
                });
                if (peer != peers.end()) {
                    state.setActiveTitle(resolvePeerTitle(*peer));
                }
            }

            state.setMessages(service::lxmf::getMessages(peerDestination));
            service::lxmf::markConversationRead(peerDestination);
        }
    } else {
        state.setMessages({});
    }
}

void ChatApp::requestViewRefresh() {
    if (!viewVisible) {
        return;
    }

    if (lvgl::lock(1000)) {
        view.refresh();
        lvgl::unlock();
    } else {
        LOGGER.warn("Failed to lock LVGL for Chat refresh");
    }
}

void ChatApp::openConversation(
    const service::reticulum::DestinationHash& peerDestination,
    const std::string& title,
    const std::string& subtitle
) {
    service::lxmf::ensureConversation(peerDestination, title, subtitle);
    state.showThread(peerDestination, title.empty() ? shortenHash(peerDestination) : title);
    state.setMessages(service::lxmf::getMessages(peerDestination));
    service::lxmf::markConversationRead(peerDestination);
    requestViewRefresh();
}

void ChatApp::onCreate(AppContext& appContext) {
    settings = settings::chat::loadOrGetDefault();
    if (settings.nickname.empty()) {
        settings.nickname = getDefaultNickname();
    }
    state.setLocalNickname(settings.nickname);
    state.showConversations();

    if (const auto pubsub = service::lxmf::getPubsub(); pubsub != nullptr) {
        lxmfSubscription = pubsub->subscribe([this](service::lxmf::LxmfEvent event) {
            switch (event.type) {
                case service::lxmf::EventType::RuntimeStateChanged:
                case service::lxmf::EventType::PeerDirectoryChanged:
                case service::lxmf::EventType::ConversationListChanged:
                case service::lxmf::EventType::MessageListChanged:
                    refreshStateFromServices();
                    requestViewRefresh();
                    break;
                default:
                    break;
            }
        });
    }

    refreshStateFromServices();
}

void ChatApp::onDestroy(AppContext& appContext) {
    if (lxmfSubscription != nullptr) {
        if (const auto pubsub = service::lxmf::getPubsub(); pubsub != nullptr) {
            pubsub->unsubscribe(lxmfSubscription);
        }
        lxmfSubscription = nullptr;
    }
}

void ChatApp::onShow(AppContext& context, lv_obj_t* parent) {
    viewVisible = true;
    view.init(context, parent);
    refreshStateFromServices();
    view.refresh();
}

void ChatApp::onHide(AppContext& context) {
    viewVisible = false;
}

void ChatApp::showConversationList() {
    state.showConversations();
    refreshStateFromServices();
    requestViewRefresh();
}

void ChatApp::showContactPicker() {
    state.showContacts();
    refreshStateFromServices();
    requestViewRefresh();
}

void ChatApp::openConversationByIndex(size_t index) {
    const auto conversations = state.getConversations();
    if (index >= conversations.size()) {
        return;
    }

    const auto& conversation = conversations[index];
    openConversation(
        conversation.peerDestination,
        resolveConversationTitle(conversation),
        conversation.subtitle
    );
}

void ChatApp::openPeerByIndex(size_t index) {
    const auto peers = state.getPeers();
    if (index >= peers.size()) {
        return;
    }

    const auto& peer = peers[index];
    openConversation(peer.destination, resolvePeerTitle(peer), peer.subtitle);
}

bool ChatApp::sendMessage(const std::string& text) {
    if (text.empty()) {
        return false;
    }

    service::reticulum::DestinationHash peerDestination;
    if (!state.getActivePeer(peerDestination)) {
        return false;
    }

    if (!service::lxmf::queueOutgoingMessage(peerDestination, state.getLocalNickname(), text)) {
        return false;
    }

    state.setMessages(service::lxmf::getMessages(peerDestination));
    requestViewRefresh();
    return true;
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
