#pragma once

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && !defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include <Tactility/app/chat/TextResources.h>
#include <Tactility/i18n/TextResources.h>

#include <format>
#include <string>

namespace tt::app::chat {

tt::i18n::TextResources& getTextResources();
std::string getLocalizedAppName();
std::string getDefaultNickname();

template <typename... Args>
std::string formatText(i18n::Text key, Args&&... args) {
    return std::vformat(getTextResources()[key], std::make_format_args(args...));
}

} // namespace tt::app::chat

#endif // CONFIG_SOC_WIFI_SUPPORTED && !CONFIG_SLAVE_SOC_WIFI_SUPPORTED
