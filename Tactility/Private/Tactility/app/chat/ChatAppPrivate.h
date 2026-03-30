#pragma once

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && !defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include "ChatState.h"
#include "ChatView.h"
#include "ChatSettings.h"

#include <Tactility/app/App.h>
#include <Tactility/PubSub.h>
#include <Tactility/service/reticulum/Reticulum.h>

namespace tt::app::chat {

class ChatApp final : public App {

    using ReticulumEvent = service::reticulum::ReticulumEvent;

    ChatState state;
    ChatView view = ChatView(this, &state);
    PubSub<ReticulumEvent>::SubscriptionHandle reticulumSubscription = nullptr;
    ChatSettingsData settings;
    bool isFirstLaunch = false;

    void ensureReticulumBindings();
    void onReticulumEvent(const ReticulumEvent& event);
    void onReceivePayload(const std::vector<uint8_t>& data);

public:
    void onCreate(AppContext& appContext) override;
    void onDestroy(AppContext& appContext) override;
    void onShow(AppContext& context, lv_obj_t* parent) override;

    void sendMessage(const std::string& text);
    void applySettings(const std::string& nickname);
    void switchChannel(const std::string& chatChannel);

    const ChatSettingsData& getSettings() const { return settings; }

    ~ChatApp() override = default;
};

} // namespace tt::app::chat

#endif // CONFIG_SOC_WIFI_SUPPORTED && !CONFIG_SLAVE_SOC_WIFI_SUPPORTED
