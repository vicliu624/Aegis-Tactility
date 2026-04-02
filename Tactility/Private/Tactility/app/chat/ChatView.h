#pragma once

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && !defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include "ChatState.h"

#include <Tactility/app/AppContext.h>

#include <lvgl.h>

namespace tt::app::chat {

class ChatApp;

class ChatView {

    ChatApp* app;
    ChatState* state;

    lv_obj_t* toolbar = nullptr;
    lv_obj_t* list = nullptr;
    lv_obj_t* inputWrapper = nullptr;
    lv_obj_t* inputField = nullptr;
    ScreenMode configuredScreenMode = ScreenMode::Conversations;
    bool toolbarConfigured = false;

    void createInputBar(lv_obj_t* parent);
    void configureToolbar();
    void refreshConversationList();
    void refreshPeerList();
    void refreshThread();
    static void configureListButton(lv_obj_t* button);
    static void onNewConversationPressed(lv_event_t* event);
    static void onBackPressed(lv_event_t* event);
    static void onConversationSelected(lv_event_t* event);
    static void onPeerSelected(lv_event_t* event);
    static void onSendClicked(lv_event_t* event);

public:
    ChatView(ChatApp* app, ChatState* state) : app(app), state(state) {}
    ~ChatView() = default;

    ChatView(const ChatView&) = delete;
    ChatView& operator=(const ChatView&) = delete;
    ChatView(ChatView&&) = delete;
    ChatView& operator=(ChatView&&) = delete;

    void init(AppContext& appContext, lv_obj_t* parent);
    void refresh();
};

} // namespace tt::app::chat

#endif // CONFIG_SOC_WIFI_SUPPORTED && !CONFIG_SLAVE_SOC_WIFI_SUPPORTED
