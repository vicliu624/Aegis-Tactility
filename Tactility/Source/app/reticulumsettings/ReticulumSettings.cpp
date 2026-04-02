#include <Tactility/Tactility.h>

#include <Tactility/Logger.h>
#include <Tactility/app/AppManifest.h>
#include <Tactility/app/LocalizedAppName.h>
#include <Tactility/app/alertdialog/AlertDialog.h>
#include <Tactility/app/reticulumsettings/TextResources.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/lvgl/Toolbar.h>
#include <Tactility/service/ServiceRegistration.h>
#include <Tactility/service/reticulum/Reticulum.h>
#include <Tactility/settings/Language.h>
#include <Tactility/settings/ReticulumSettings.h>

#include <tactility/lvgl_icon_shared.h>

#include <charconv>
#include <format>
#include <lvgl.h>

namespace tt::app::reticulumsettings {

#ifdef ESP_PLATFORM
constexpr auto* TEXT_RESOURCE_PATH = "/system/app/ReticulumSettings/i18n";
#else
constexpr auto* TEXT_RESOURCE_PATH = "system/app/ReticulumSettings/i18n";
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
    return tt::app::getLocalizedAppNameFromPath(TEXT_RESOURCE_PATH);
}

template <typename... Args>
static std::string formatText(i18n::Text key, Args&&... args) {
    return std::vformat(getTextResources()[key], std::make_format_args(args...));
}

class ReticulumSettingsApp final : public App {

    Logger logger = Logger("ReticulumSettings");
    lv_obj_t* statusLabel = nullptr;
    lv_obj_t* spinner = nullptr;
    lv_obj_t* enabledSwitch = nullptr;
    lv_obj_t* frequencyTextArea = nullptr;
    lv_obj_t* bandwidthTextArea = nullptr;
    lv_obj_t* txPowerTextArea = nullptr;
    lv_obj_t* spreadingFactorTextArea = nullptr;
    lv_obj_t* codingRateTextArea = nullptr;

    static bool parseUint32(const char* text, uint32_t& out) {
        if (text == nullptr) {
            return false;
        }

        const auto length = strlen(text);
        if (length == 0) {
            return false;
        }

        const auto result = std::from_chars(text, text + length, out);
        return result.ec == std::errc {} && result.ptr == text + length;
    }

    void applySettingsToViews(const settings::reticulum::LoRaSettings& settings) {
        lv_obj_set_state(enabledSwitch, settings.enabled ? LV_STATE_CHECKED : 0, true);

        lv_textarea_set_text(frequencyTextArea, std::to_string(settings.frequency).c_str());
        lv_textarea_set_text(bandwidthTextArea, std::to_string(settings.bandwidth).c_str());
        lv_textarea_set_text(txPowerTextArea, std::to_string(settings.txPower).c_str());
        lv_textarea_set_text(spreadingFactorTextArea, std::to_string(settings.spreadingFactor).c_str());
        lv_textarea_set_text(codingRateTextArea, std::to_string(settings.codingRate).c_str());
    }

    void updateStatusText(const std::string& detail = {}) {
        const auto serviceState = service::getState("Reticulum");
        const auto serviceText = serviceState == service::State::Stopped
            ? getTextResources()[i18n::Text::SERVICE_STOPPED]
            : getTextResources()[i18n::Text::SERVICE_RUNNING];

        auto interfaceText = getTextResources()[i18n::Text::INTERFACE_INACTIVE];
        for (const auto& descriptor : service::reticulum::getInterfaces()) {
            if (descriptor.kind == service::reticulum::InterfaceKind::LoRa) {
                interfaceText = descriptor.started && descriptor.metrics.available
                    ? getTextResources()[i18n::Text::INTERFACE_ACTIVE]
                    : getTextResources()[i18n::Text::INTERFACE_INACTIVE];
                break;
            }
        }

        auto label = formatText(i18n::Text::STATUS_FMT, serviceText, interfaceText);
        if (!detail.empty()) {
            label = std::format("{}\n{}", label, detail);
        }
        lv_label_set_text(statusLabel, label.c_str());
    }

    settings::reticulum::LoRaSettings collectSettingsFromViews() const {
        settings::reticulum::LoRaSettings settings = settings::reticulum::getDefault();
        settings.enabled = lv_obj_has_state(enabledSwitch, LV_STATE_CHECKED);

        uint32_t parsed = 0;
        parseUint32(lv_textarea_get_text(frequencyTextArea), parsed);
        settings.frequency = parsed;
        parseUint32(lv_textarea_get_text(bandwidthTextArea), parsed);
        settings.bandwidth = parsed;
        parseUint32(lv_textarea_get_text(txPowerTextArea), parsed);
        settings.txPower = static_cast<uint8_t>(parsed);
        parseUint32(lv_textarea_get_text(spreadingFactorTextArea), parsed);
        settings.spreadingFactor = static_cast<uint8_t>(parsed);
        parseUint32(lv_textarea_get_text(codingRateTextArea), parsed);
        settings.codingRate = static_cast<uint8_t>(parsed);

        return settings;
    }

    static void onSavePressed(lv_event_t* event) {
        auto* app = static_cast<ReticulumSettingsApp*>(lv_event_get_user_data(event));
        app->saveSettings();
    }

    void saveSettings() {
        const auto copy = collectSettingsFromViews();
        std::optional<std::string> validationError;
        if (!settings::reticulum::validate(copy, validationError)) {
            alertdialog::start(
                getTextResources()[i18n::Text::VALIDATION_TITLE],
                validationError.value_or(getTextResources()[i18n::Text::SAVE_FAILED])
            );
            return;
        }

        lv_obj_remove_flag(spinner, LV_OBJ_FLAG_HIDDEN);
        updateStatusText(getTextResources()[i18n::Text::APPLYING]);

        getMainDispatcher().dispatch([this, copy] {
            const auto saved = settings::reticulum::save(copy);
            bool restarted = false;
            if (saved) {
                service::stopService("Reticulum");
                restarted = service::startService("Reticulum");
            }

            auto lock = lvgl::getSyncLock()->asScopedLock();
            if (lock.lock(500 / portTICK_PERIOD_MS)) {
                lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
                if (saved && restarted) {
                    updateStatusText(getTextResources()[i18n::Text::SAVE_SUCCESS]);
                } else if (!saved) {
                    updateStatusText(getTextResources()[i18n::Text::SAVE_FAILED]);
                } else {
                    updateStatusText(getTextResources()[i18n::Text::RESTART_FAILED]);
                }
            }

            if (!saved) {
                alertdialog::start(
                    getTextResources()[i18n::Text::VALIDATION_TITLE],
                    getTextResources()[i18n::Text::SAVE_FAILED]
                );
            } else if (!restarted) {
                alertdialog::start(
                    getTextResources()[i18n::Text::VALIDATION_TITLE],
                    getTextResources()[i18n::Text::RESTART_FAILED]
                );
            }
        });
    }

    lv_obj_t* createRow(lv_obj_t* parent, const std::string& labelText) {
        auto* row = lv_obj_create(parent);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 8, 0);
        lv_obj_set_style_pad_column(row, 8, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        auto* label = lv_label_create(row);
        lv_label_set_text(label, labelText.c_str());
        return row;
    }

    lv_obj_t* createSingleLineTextArea(lv_obj_t* parent) {
        auto* textArea = lv_textarea_create(parent);
        lv_textarea_set_one_line(textArea, true);
        lv_textarea_set_max_length(textArea, 16);
        lv_obj_set_width(textArea, 160);
        return textArea;
    }

public:

    void onShow(AppContext& app, lv_obj_t* parent) override {
        lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

        auto* toolbar = lvgl::toolbar_create(parent, app);
        spinner = lvgl::toolbar_add_spinner_action(toolbar);
        lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);

        auto* mainWrapper = lv_obj_create(parent);
        lv_obj_set_flex_flow(mainWrapper, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_width(mainWrapper, LV_PCT(100));
        lv_obj_set_flex_grow(mainWrapper, 1);

        auto* statusRow = createRow(mainWrapper, getLocalizedAppName());
        lv_obj_set_flex_flow(statusRow, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(statusRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        statusLabel = lv_label_create(statusRow);
        lv_label_set_long_mode(statusLabel, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(statusLabel, LV_PCT(100));

        auto* enabledRow = createRow(mainWrapper, getTextResources()[i18n::Text::ENABLED]);
        enabledSwitch = lv_switch_create(enabledRow);

        auto* frequencyRow = createRow(mainWrapper, getTextResources()[i18n::Text::FREQUENCY]);
        frequencyTextArea = createSingleLineTextArea(frequencyRow);

        auto* bandwidthRow = createRow(mainWrapper, getTextResources()[i18n::Text::BANDWIDTH]);
        bandwidthTextArea = createSingleLineTextArea(bandwidthRow);

        auto* txPowerRow = createRow(mainWrapper, getTextResources()[i18n::Text::TX_POWER]);
        txPowerTextArea = createSingleLineTextArea(txPowerRow);

        auto* sfRow = createRow(mainWrapper, getTextResources()[i18n::Text::SPREADING_FACTOR]);
        spreadingFactorTextArea = createSingleLineTextArea(sfRow);

        auto* crRow = createRow(mainWrapper, getTextResources()[i18n::Text::CODING_RATE]);
        codingRateTextArea = createSingleLineTextArea(crRow);

        auto* buttonRow = lv_obj_create(mainWrapper);
        lv_obj_set_width(buttonRow, LV_PCT(100));
        lv_obj_set_height(buttonRow, LV_SIZE_CONTENT);
        lv_obj_set_style_border_width(buttonRow, 0, 0);
        lv_obj_set_style_pad_all(buttonRow, 8, 0);

        auto* saveButton = lv_button_create(buttonRow);
        lv_obj_center(saveButton);
        auto* saveLabel = lv_label_create(saveButton);
        lv_label_set_text(saveLabel, getTextResources()[i18n::Text::SAVE].c_str());
        lv_obj_add_event_cb(saveButton, onSavePressed, LV_EVENT_SHORT_CLICKED, this);

        applySettingsToViews(settings::reticulum::loadOrGetDefault());
        updateStatusText();
    }
};

extern const AppManifest manifest = {
    .appId = "ReticulumSettings",
    .appName = "Reticulum LoRa",
    .resolveLocalizedAppName = &getLocalizedAppName,
    .appIcon = LVGL_ICON_SHARED_NAVIGATION,
    .appCategory = Category::Settings,
    .createApp = create<ReticulumSettingsApp>
};

void start() {
    app::start(manifest.appId);
}

} // namespace tt::app::reticulumsettings
