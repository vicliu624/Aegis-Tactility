#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && !defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include <Tactility/Tactility.h>

#include <Tactility/app/AppManifest.h>
#include <Tactility/app/LocalizedAppName.h>
#include <Tactility/app/alertdialog/AlertDialog.h>
#include <Tactility/app/chat/Localization.h>
#include <Tactility/app/chatprofile/TextResources.h>
#include <Tactility/lvgl/Toolbar.h>
#include <Tactility/settings/ChatSettings.h>
#include <Tactility/settings/Language.h>

#include <tactility/lvgl_icon_shared.h>

#include <format>
#include <lvgl.h>
#include <optional>

namespace tt::app::chatprofile {

#ifdef ESP_PLATFORM
constexpr auto* TEXT_RESOURCE_PATH = "/system/app/ChatProfile/i18n";
#else
constexpr auto* TEXT_RESOURCE_PATH = "system/app/ChatProfile/i18n";
#endif

constexpr uint32_t MAX_NICKNAME_LENGTH = 64;

static tt::i18n::TextResources& getTextResources() {
    static tt::i18n::TextResources textResources(TEXT_RESOURCE_PATH);
    static std::string loadedLocale;

    const auto currentLocale = tt::settings::toString(tt::settings::getLanguage());
    if (loadedLocale != currentLocale) {
        textResources.load();
        loadedLocale = currentLocale;
    }

    return textResources;
}

static std::string getLocalizedAppName() {
    return tt::app::getLocalizedAppNameFromPath(TEXT_RESOURCE_PATH);
}

template <typename... Args>
static std::string formatText(i18n::Text key, Args&&... args) {
    return std::vformat(getTextResources()[key], std::make_format_args(args...));
}

class ChatProfileApp final : public App {

    lv_obj_t* nicknameInput = nullptr;

    static void onSavePressed(lv_event_t* event) {
        auto* app = static_cast<ChatProfileApp*>(lv_event_get_user_data(event));
        app->save();
    }

    void save() {
        auto settings = settings::chat::loadOrGetDefault();
        settings.nickname = lv_textarea_get_text(nicknameInput);
        std::optional<std::string> error;
        if (!settings::chat::validate(settings, error)) {
            alertdialog::start(
                getTextResources()[i18n::Text::VALIDATION_TITLE],
                error.value_or(getTextResources()[i18n::Text::EMPTY_NICKNAME])
            );
            return;
        }

        if (!settings::chat::save(settings)) {
            alertdialog::start(
                getTextResources()[i18n::Text::VALIDATION_TITLE],
                getTextResources()[i18n::Text::SAVE_FAILED]
            );
            return;
        }

        alertdialog::start(
            getTextResources()[i18n::Text::APP_NAME],
            getTextResources()[i18n::Text::SAVE_SUCCESS]
        );
    }

public:

    void onShow(AppContext& appContext, lv_obj_t* parent) override {
        lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

        auto* toolbar = lvgl::toolbar_create(parent, appContext);
        lvgl::toolbar_set_title(toolbar, getLocalizedAppName());

        auto* wrapper = lv_obj_create(parent);
        lv_obj_set_width(wrapper, LV_PCT(100));
        lv_obj_set_flex_grow(wrapper, 1);
        lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(wrapper, 8, 0);
        lv_obj_set_style_pad_row(wrapper, 8, 0);

        auto* nicknameLabel = lv_label_create(wrapper);
        const auto label = formatText(i18n::Text::NICKNAME_LABEL_FMT, MAX_NICKNAME_LENGTH);
        lv_label_set_text(nicknameLabel, label.c_str());

        nicknameInput = lv_textarea_create(wrapper);
        lv_obj_set_width(nicknameInput, LV_PCT(100));
        lv_textarea_set_one_line(nicknameInput, true);
        lv_textarea_set_max_length(nicknameInput, MAX_NICKNAME_LENGTH);
        auto settings = settings::chat::loadOrGetDefault();
        if (settings.nickname.empty()) {
            settings.nickname = chat::getDefaultNickname();
        }
        lv_textarea_set_text(nicknameInput, settings.nickname.c_str());

        auto* saveButton = lv_button_create(wrapper);
        lv_obj_add_event_cb(saveButton, onSavePressed, LV_EVENT_SHORT_CLICKED, this);
        auto* saveLabel = lv_label_create(saveButton);
        lv_label_set_text(saveLabel, getTextResources()[i18n::Text::SAVE].c_str());
        lv_obj_center(saveLabel);
    }
};

extern const AppManifest manifest = {
    .appId = "ChatProfile",
    .appName = "Chat Settings",
    .resolveLocalizedAppName = &getLocalizedAppName,
    .appIcon = LVGL_ICON_SHARED_FORUM,
    .appCategory = Category::Settings,
    .createApp = create<ChatProfileApp>
};

} // namespace tt::app::chatprofile

#endif // CONFIG_SOC_WIFI_SUPPORTED && !CONFIG_SLAVE_SOC_WIFI_SUPPORTED
