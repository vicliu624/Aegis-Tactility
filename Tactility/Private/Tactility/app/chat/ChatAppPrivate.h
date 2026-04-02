#pragma once

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && !defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include "ChatState.h"
#include "ChatView.h"

#include <Tactility/app/App.h>
#include <Tactility/PubSub.h>
#include <Tactility/service/lxmf/Events.h>
#include <Tactility/settings/ChatSettings.h>

namespace tt::app::chat {

class ChatApp final : public App {

    ChatState state;
    ChatView view = ChatView(this, &state);
    settings::chat::ChatSettings settings;
    PubSub<service::lxmf::LxmfEvent>::SubscriptionHandle lxmfSubscription = nullptr;
    bool viewVisible = false;

    void refreshStateFromServices();
    void requestViewRefresh();
    void openConversation(
        const service::reticulum::DestinationHash& peerDestination,
        const std::string& title,
        const std::string& subtitle = {}
    );

public:
    void onCreate(AppContext& appContext) override;
    void onDestroy(AppContext& appContext) override;
    void onShow(AppContext& context, lv_obj_t* parent) override;
    void onHide(AppContext& context) override;

    bool sendMessage(const std::string& text);
    void showConversationList();
    void showContactPicker();
    void openConversationByIndex(size_t index);
    void openPeerByIndex(size_t index);

    ~ChatApp() override = default;
};

} // namespace tt::app::chat

#endif // CONFIG_SOC_WIFI_SUPPORTED && !CONFIG_SLAVE_SOC_WIFI_SUPPORTED
