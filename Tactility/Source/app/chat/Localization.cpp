#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && !defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include <Tactility/app/chat/Localization.h>
#include <Tactility/settings/Language.h>

namespace tt::app::chat {

#ifdef ESP_PLATFORM
constexpr auto* TEXT_RESOURCE_PATH = "/system/app/Chat/i18n";
#else
constexpr auto* TEXT_RESOURCE_PATH = "system/app/Chat/i18n";
#endif

tt::i18n::TextResources& getTextResources() {
    static tt::i18n::TextResources textResources(TEXT_RESOURCE_PATH);
    static std::string loadedLocale;

    const auto currentLocale = tt::settings::toString(tt::settings::getLanguage());
    if (loadedLocale != currentLocale) {
        textResources.load();
        loadedLocale = currentLocale;
    }

    return textResources;
}

std::string getLocalizedAppName() {
    return getTextResources()[i18n::Text::APP_NAME];
}

std::string getDefaultNickname() {
    return getTextResources()[i18n::Text::DEFAULT_NICKNAME];
}

} // namespace tt::app::chat

#endif // CONFIG_SOC_WIFI_SUPPORTED && !CONFIG_SLAVE_SOC_WIFI_SUPPORTED
