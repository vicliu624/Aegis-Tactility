#include <Tactility/app/AppManifest.h>
#include <Tactility/app/timedatesettings/TextResources.h>
#include <Tactility/app/timezone/TimeZone.h>
#include <Tactility/Logger.h>
#include <Tactility/lvgl/Toolbar.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/RecursiveMutex.h>
#include <Tactility/settings/Language.h>
#include <Tactility/service/loader/Loader.h>
#include <Tactility/settings/Time.h>
#include <Tactility/settings/SystemSettings.h>

#include <lvgl.h>

#include <tactility/lvgl_icon_shared.h>

namespace tt::app::timedatesettings {

static const auto LOGGER = Logger("TimeDate");

#ifdef ESP_PLATFORM
constexpr auto* TEXT_RESOURCE_PATH = "/system/app/TimeDateSettings/i18n";
#else
constexpr auto* TEXT_RESOURCE_PATH = "system/app/TimeDateSettings/i18n";
#endif

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
    return getTextResources()[i18n::Text::APP_NAME];
}

extern const AppManifest manifest;

class TimeDateSettingsApp final : public App {

    RecursiveMutex mutex;
    lv_obj_t* timeZoneLabel = nullptr;
    lv_obj_t* dateFormatDropdown = nullptr;

    static void onTimeFormatChanged(lv_event_t* event) {
        auto* widget = lv_event_get_target_obj(event);
        bool show_24 = lv_obj_has_state(widget, LV_STATE_CHECKED);
        settings::setTimeFormat24Hour(show_24);
    }

    static void onTimeZonePressed(lv_event_t* event) {
        timezone::start();
    }

    static void onDateFormatChanged(lv_event_t* event) {
        auto* dropdown = static_cast<lv_obj_t*>(lv_event_get_target(event));
        auto index = lv_dropdown_get_selected(dropdown);
        
        const char* dateFormats[] = {"MM/DD/YYYY", "DD/MM/YYYY", "YYYY-MM-DD", "YYYY/MM/DD"};
        std::string selected_format = dateFormats[index];
        
        settings::SystemSettings sysSettings;
        if (settings::loadSystemSettings(sysSettings)) {
            sysSettings.dateFormat = selected_format;
            settings::saveSystemSettings(sysSettings);
        }
    }

public:

    void onShow(AppContext& app, lv_obj_t* parent) override {
        auto& textResources = getTextResources();
        lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

        lvgl::toolbar_create(parent, app);

        auto* main_wrapper = lv_obj_create(parent);
        lv_obj_set_flex_flow(main_wrapper, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_width(main_wrapper, LV_PCT(100));
        lv_obj_set_flex_grow(main_wrapper, 1);

        // 24-hour format toggle

        auto* time_format_wrapper = lv_obj_create(main_wrapper);
        lv_obj_set_width(time_format_wrapper, LV_PCT(100));
        lv_obj_set_height(time_format_wrapper, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(time_format_wrapper, 8, 0);
        lv_obj_set_style_border_width(time_format_wrapper, 0, 0);

        auto* time_24h_label = lv_label_create(time_format_wrapper);
        lv_label_set_text(time_24h_label, textResources[i18n::Text::TIME_FORMAT_24H].c_str());
        lv_obj_align(time_24h_label, LV_ALIGN_LEFT_MID, 4, 0);

        auto* time_24h_switch = lv_switch_create(time_format_wrapper);
        lv_obj_align(time_24h_switch, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(time_24h_switch, onTimeFormatChanged, LV_EVENT_VALUE_CHANGED, nullptr);
        if (settings::isTimeFormat24Hour()) {
            lv_obj_add_state(time_24h_switch, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(time_24h_switch, LV_STATE_CHECKED);
        }

        // Date format dropdown

        auto* date_format_wrapper = lv_obj_create(main_wrapper);
        lv_obj_set_width(date_format_wrapper, LV_PCT(100));
        lv_obj_set_height(date_format_wrapper, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(date_format_wrapper, 8, 0);
        lv_obj_set_style_border_width(date_format_wrapper, 0, 0);

        auto* date_format_label = lv_label_create(date_format_wrapper);
        lv_label_set_text(date_format_label, textResources[i18n::Text::DATE_FORMAT].c_str());
        lv_obj_align(date_format_label, LV_ALIGN_LEFT_MID, 4, 0);

        dateFormatDropdown = lv_dropdown_create(date_format_wrapper);
        lv_obj_set_width(dateFormatDropdown, 150);
        lv_obj_align(dateFormatDropdown, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_dropdown_set_options(dateFormatDropdown, "MM/DD/YYYY\nDD/MM/YYYY\nYYYY-MM-DD\nYYYY/MM/DD");
        
        settings::SystemSettings sysSettings;
        if (settings::loadSystemSettings(sysSettings)) {
            int index = 0;
            if (sysSettings.dateFormat == "DD/MM/YYYY") index = 1;
            else if (sysSettings.dateFormat == "YYYY-MM-DD") index = 2;
            else if (sysSettings.dateFormat == "YYYY/MM/DD") index = 3;
            lv_dropdown_set_selected(dateFormatDropdown, index);
        }
        lv_obj_add_event_cb(dateFormatDropdown, onDateFormatChanged, LV_EVENT_VALUE_CHANGED, nullptr);

        // Timezone selector

        auto* timezone_wrapper = lv_obj_create(main_wrapper);
        lv_obj_set_width(timezone_wrapper, LV_PCT(100));
        lv_obj_set_height(timezone_wrapper, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(timezone_wrapper, 8, 0);
        lv_obj_set_style_border_width(timezone_wrapper, 0, 0);

        auto* timezone_label = lv_label_create(timezone_wrapper);
        lv_label_set_text(timezone_label, textResources[i18n::Text::TIMEZONE].c_str());
        lv_obj_align(timezone_label, LV_ALIGN_LEFT_MID, 4, 0);

        auto* timezone_button = lv_button_create(timezone_wrapper);
        lv_obj_set_width(timezone_button, 150);
        lv_obj_align(timezone_button, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(timezone_button, onTimeZonePressed, LV_EVENT_SHORT_CLICKED, nullptr);

        timeZoneLabel = lv_label_create(timezone_button);
        std::string timeZoneName = settings::getTimeZoneName();
        if (timeZoneName.empty()) {
            timeZoneName = textResources[i18n::Text::NOT_SET];
        }
        lv_obj_center(timeZoneLabel);
        lv_label_set_text(timeZoneLabel, timeZoneName.c_str());
    }

    void onResult(AppContext& app, LaunchId launchId, Result result, std::unique_ptr<Bundle> bundle) override {
        if (result == Result::Ok && bundle != nullptr) {
            const auto name = timezone::getResultName(*bundle);
            const auto code = timezone::getResultCode(*bundle);
            LOGGER.info("Result name={} code={}", name, code);
            settings::setTimeZone(name, code);

            if (!name.empty()) {
                if (lvgl::lock(100 / portTICK_PERIOD_MS)) {
                    lv_label_set_text(timeZoneLabel, name.c_str());
                    lvgl::unlock();
                }
            }
        }
    }
};

extern const AppManifest manifest = {
    .appId = "TimeDateSettings",
    .appName = "Time & Date",
    .resolveLocalizedAppName = &getLocalizedAppName,
    .appIcon = LVGL_ICON_SHARED_CALENDAR_MONTH,
    .appCategory = Category::Settings,
    .createApp = create<TimeDateSettingsApp>
};

LaunchId start() {
    return app::start(manifest.appId);
}

} // namespace

