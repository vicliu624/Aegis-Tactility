#include <Tactility/app/App.h>
#include <Tactility/app/AppManifest.h>
#include <Tactility/app/usbsettings/TextResources.h>
#include <Tactility/hal/usb/Usb.h>
#include <Tactility/lvgl/Toolbar.h>
#include <Tactility/settings/Language.h>

#include <lvgl.h>

#include <tactility/lvgl_icon_shared.h>

#define TAG "usb_settings"

namespace tt::app::usbsettings {

#ifdef ESP_PLATFORM
constexpr auto* TEXT_RESOURCE_PATH = "/system/app/UsbSettings/i18n";
#else
constexpr auto* TEXT_RESOURCE_PATH = "system/app/UsbSettings/i18n";
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

static void onRebootMassStorageSdmmc(lv_event_t* event) {
    hal::usb::rebootIntoMassStorageSdmmc();
}

// Flash reboot handler
static void onRebootMassStorageFlash(lv_event_t* event) {
    hal::usb::rebootIntoMassStorageFlash();
}

class UsbSettingsApp : public App {

    void onShow(AppContext& app, lv_obj_t* parent) override {
        auto& textResources = getTextResources();
        auto* toolbar = lvgl::toolbar_create(parent, app);
        lv_obj_align(toolbar, LV_ALIGN_TOP_MID, 0, 0);

        // Create a wrapper container for buttons
        auto* wrapper = lv_obj_create(parent);
        lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(wrapper, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_size(wrapper, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_align(wrapper, LV_ALIGN_CENTER, 0, 0);

        bool hasSd = hal::usb::canRebootIntoMassStorageSdmmc();
        bool hasFlash = hal::usb::canRebootIntoMassStorageFlash();

        if (hasSd) {
            auto* button_sd = lv_button_create(wrapper);
            auto* label_sd = lv_label_create(button_sd);
            lv_label_set_text(label_sd, textResources[i18n::Text::REBOOT_AS_USB_STORAGE_SD].c_str());
            lv_obj_add_event_cb(button_sd, onRebootMassStorageSdmmc, LV_EVENT_SHORT_CLICKED, nullptr);
        }

        if (hasFlash) {
            auto* button_flash = lv_button_create(wrapper);
            auto* label_flash = lv_label_create(button_flash);
            lv_label_set_text(label_flash, textResources[i18n::Text::REBOOT_AS_USB_STORAGE_FLASH].c_str());
            lv_obj_add_event_cb(button_flash, onRebootMassStorageFlash, LV_EVENT_SHORT_CLICKED, nullptr);
        }

        if (!hasSd && !hasFlash) {
            bool supported = hal::usb::isSupported();
            auto* label = lv_label_create(wrapper);
            lv_label_set_text(
                label,
                supported
                    ? textResources[i18n::Text::USB_STORAGE_NOT_AVAILABLE].c_str()
                    : textResources[i18n::Text::USB_DRIVER_NOT_SUPPORTED].c_str()
            );
        }
    }
};

extern const AppManifest manifest = {
    .appId = "UsbSettings",
    .appName = "USB",
    .resolveLocalizedAppName = &getLocalizedAppName,
    .appIcon = LVGL_ICON_SHARED_USB,
    .appCategory = Category::Settings,
    .createApp = create<UsbSettingsApp>
};

} // namespace
