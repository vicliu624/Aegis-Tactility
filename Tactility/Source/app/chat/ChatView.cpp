#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && !defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include <Tactility/app/chat/ChatView.h>
#include <Tactility/app/chat/ChatAppPrivate.h>
#include <Tactility/app/chat/Localization.h>

#include <Tactility/app/App.h>
#include <Tactility/lvgl/Toolbar.h>

#include <cstring>
#include <format>

namespace tt::app::chat {

namespace {

constexpr uint32_t MAX_COMPOSE_LENGTH = 1024;

static std::string deliveryStateLabel(service::lxmf::DeliveryState state) {
    switch (state) {
        case service::lxmf::DeliveryState::Queued:
            return getTextResources()[i18n::Text::STATUS_QUEUED];
        case service::lxmf::DeliveryState::Sending:
            return getTextResources()[i18n::Text::STATUS_SENDING];
        case service::lxmf::DeliveryState::Delivered:
            return getTextResources()[i18n::Text::STATUS_DELIVERED];
        case service::lxmf::DeliveryState::Failed:
            return getTextResources()[i18n::Text::STATUS_FAILED];
    }
    return getTextResources()[i18n::Text::STATUS_QUEUED];
}

} // namespace

void ChatView::configureListButton(lv_obj_t* button) {
    if (button == nullptr) {
        return;
    }

    lv_obj_set_height(button, LV_SIZE_CONTENT);
    if (auto* icon = lv_obj_get_child(button, 0); icon != nullptr) {
        lv_obj_set_style_text_opa(icon, LV_OPA_80, LV_PART_MAIN);
    }

    if (auto* label = lv_obj_get_child(button, 1); label != nullptr) {
        lv_obj_set_width(label, LV_PCT(100));
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_line_space(label, 4, LV_PART_MAIN);
    }
}

void ChatView::init(AppContext& appContext, lv_obj_t* parent) {
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    toolbar = lvgl::toolbar_create(parent, appContext);
    lvgl::toolbar_set_nav_action(toolbar, LV_SYMBOL_LEFT, onBackPressed, this);

    list = lv_list_create(parent);
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_style_border_width(list, 0, 0);

    createInputBar(parent);
}

void ChatView::createInputBar(lv_obj_t* parent) {
    inputWrapper = lv_obj_create(parent);
    lv_obj_set_flex_flow(inputWrapper, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(inputWrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(inputWrapper, 0, 0);
    lv_obj_set_style_pad_column(inputWrapper, 4, 0);
    lv_obj_set_style_border_opa(inputWrapper, 0, LV_STATE_DEFAULT);

    inputField = lv_textarea_create(inputWrapper);
    lv_obj_set_flex_grow(inputField, 1);
    lv_textarea_set_placeholder_text(inputField, getTextResources()[i18n::Text::MESSAGE_PLACEHOLDER].c_str());
    lv_textarea_set_one_line(inputField, true);
    lv_textarea_set_max_length(inputField, MAX_COMPOSE_LENGTH);

    auto* sendButton = lv_button_create(inputWrapper);
    lv_obj_set_style_margin_all(sendButton, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_margin_top(sendButton, 2, LV_STATE_DEFAULT);
    lv_obj_add_event_cb(sendButton, onSendClicked, LV_EVENT_CLICKED, this);

    auto* buttonLabel = lv_label_create(sendButton);
    lv_label_set_text(buttonLabel, getTextResources()[i18n::Text::SEND].c_str());
    lv_obj_center(buttonLabel);
}

void ChatView::configureToolbar() {
    if (toolbar == nullptr) {
        return;
    }

    const auto screenMode = state->getScreenMode();

    if (!toolbarConfigured || configuredScreenMode != screenMode) {
        lvgl::toolbar_clear_actions(toolbar);

        switch (screenMode) {
            case ScreenMode::Conversations:
                lvgl::toolbar_add_text_button_action(toolbar, getTextResources()[i18n::Text::NEW].c_str(), onNewConversationPressed, this);
                break;
            case ScreenMode::Contacts:
                break;
            case ScreenMode::Thread:
                break;
        }

        toolbarConfigured = true;
        configuredScreenMode = screenMode;
    }

    switch (screenMode) {
        case ScreenMode::Conversations:
            lvgl::toolbar_set_title(toolbar, getLocalizedAppName());
            break;
        case ScreenMode::Contacts:
            lvgl::toolbar_set_title(toolbar, getTextResources()[i18n::Text::NEW_MESSAGE_TITLE]);
            break;
        case ScreenMode::Thread:
            lvgl::toolbar_set_title(toolbar, state->getActiveTitle());
            break;
    }
}

void ChatView::refreshConversationList() {
    lv_obj_add_flag(inputWrapper, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clean(list);

    const auto conversations = state->getConversations();
    if (conversations.empty()) {
        lv_list_add_text(list, getTextResources()[i18n::Text::NO_CONVERSATIONS].c_str());
        return;
    }

    for (size_t index = 0; index < conversations.size(); index++) {
        const auto& conversation = conversations[index];
        const auto preview = conversation.preview.empty()
            ? getTextResources()[i18n::Text::NO_MESSAGES_YET]
            : conversation.preview;
        const auto title = conversation.unreadCount > 0
            ? std::format("[{}] {}", conversation.unreadCount, conversation.title)
            : conversation.title;
        const auto line = std::format("{}\n{}\n{}", title, preview, conversation.subtitle);
        auto* button = lv_list_add_button(list, conversation.reachable ? LV_SYMBOL_OK : LV_SYMBOL_WARNING, line.c_str());
        lv_obj_set_user_data(button, reinterpret_cast<void*>(index));
        lv_obj_add_event_cb(button, onConversationSelected, LV_EVENT_SHORT_CLICKED, this);
        configureListButton(button);
    }
}

void ChatView::refreshPeerList() {
    lv_obj_add_flag(inputWrapper, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clean(list);

    const auto peers = state->getPeers();
    if (peers.empty()) {
        lv_list_add_text(list, getTextResources()[i18n::Text::NO_CONTACTS].c_str());
        return;
    }

    for (size_t index = 0; index < peers.size(); index++) {
        const auto& peer = peers[index];
        const auto line = std::format("{}\n{}", peer.title, peer.subtitle);
        auto* button = lv_list_add_button(list, peer.reachable ? LV_SYMBOL_OK : LV_SYMBOL_WARNING, line.c_str());
        lv_obj_set_user_data(button, reinterpret_cast<void*>(index));
        lv_obj_add_event_cb(button, onPeerSelected, LV_EVENT_SHORT_CLICKED, this);
        configureListButton(button);
    }
}

void ChatView::refreshThread() {
    lv_obj_clear_flag(inputWrapper, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clean(list);

    const auto messages = state->getMessages();
    if (messages.empty()) {
        lv_list_add_text(list, getTextResources()[i18n::Text::NO_MESSAGES_YET].c_str());
        return;
    }

    for (const auto& message : messages) {
        auto* label = lv_label_create(list);
        std::string header = message.author;
        if (message.direction == service::lxmf::MessageDirection::Outgoing) {
            header = std::format("{}  {}", message.author, deliveryStateLabel(message.deliveryState));
        }

        const auto text = std::format("{}\n{}", header, message.body);
        lv_label_set_text(label, text.c_str());
        lv_obj_set_width(label, lv_pct(100));
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_pad_all(label, 6, 0);

        if (message.direction == service::lxmf::MessageDirection::Outgoing
            && lv_display_get_color_format(lv_obj_get_display(label)) != LV_COLOR_FORMAT_L8) {
            lv_obj_set_style_text_color(label, lv_color_hex(0x80C0FF), 0);
        }
    }

    lv_obj_scroll_to_y(list, LV_COORD_MAX, LV_ANIM_OFF);
}

void ChatView::refresh() {
    configureToolbar();

    switch (state->getScreenMode()) {
        case ScreenMode::Conversations:
            refreshConversationList();
            break;
        case ScreenMode::Contacts:
            refreshPeerList();
            break;
        case ScreenMode::Thread:
            refreshThread();
            break;
    }
}

void ChatView::onNewConversationPressed(lv_event_t* event) {
    auto* self = static_cast<ChatView*>(lv_event_get_user_data(event));
    self->app->showContactPicker();
}

void ChatView::onBackPressed(lv_event_t* event) {
    auto* self = static_cast<ChatView*>(lv_event_get_user_data(event));
    if (self->state->getScreenMode() == ScreenMode::Conversations) {
        app::stop();
    } else {
        self->app->showConversationList();
    }
}

void ChatView::onConversationSelected(lv_event_t* event) {
    auto* self = static_cast<ChatView*>(lv_event_get_user_data(event));
    auto* button = lv_event_get_current_target_obj(event);
    const auto index = reinterpret_cast<size_t>(lv_obj_get_user_data(button));
    self->app->openConversationByIndex(index);
}

void ChatView::onPeerSelected(lv_event_t* event) {
    auto* self = static_cast<ChatView*>(lv_event_get_user_data(event));
    auto* button = lv_event_get_current_target_obj(event);
    const auto index = reinterpret_cast<size_t>(lv_obj_get_user_data(button));
    self->app->openPeerByIndex(index);
}

void ChatView::onSendClicked(lv_event_t* event) {
    auto* self = static_cast<ChatView*>(lv_event_get_user_data(event));
    const auto* text = lv_textarea_get_text(self->inputField);
    if (text != nullptr && strlen(text) > 0) {
        if (self->app->sendMessage(std::string(text))) {
            lv_textarea_set_text(self->inputField, "");
        }
    }
}

} // namespace tt::app::chat

#endif // CONFIG_SOC_WIFI_SUPPORTED && !CONFIG_SLAVE_SOC_WIFI_SUPPORTED
