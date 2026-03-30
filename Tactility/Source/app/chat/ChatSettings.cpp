#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && !defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include <Tactility/app/chat/ChatSettings.h>
#include <Tactility/app/chat/Localization.h>
#include <Tactility/app/chat/ChatProtocol.h>

#include <Tactility/file/PropertiesFile.h>

#include <esp_random.h>

#include <cstdlib>
#include <map>
#include <unistd.h>

namespace tt::app::chat {

constexpr auto* KEY_SENDER_ID = "senderId";
constexpr auto* KEY_NICKNAME = "nickname";
constexpr auto* KEY_CHAT_CHANNEL = "chatChannel";

/** Generate a non-zero random sender ID using hardware RNG. */
static uint32_t generateSenderId() {
    uint32_t id;
    do {
        id = esp_random();
    } while (id == 0);
    return id;
}

ChatSettingsData getDefaultSettings() {
    return ChatSettingsData{
        .senderId = 0,
        .nickname = getDefaultNickname(),
        .chatChannel = "#general"
    };
}

ChatSettingsData loadSettings() {
    ChatSettingsData settings = getDefaultSettings();

    std::map<std::string, std::string> map;
    if (!file::loadPropertiesFile(CHAT_SETTINGS_FILE, map)) {
        settings.senderId = generateSenderId();
        return settings;
    }

    auto it = map.find(KEY_SENDER_ID);
    if (it != map.end() && !it->second.empty()) {
        settings.senderId = static_cast<uint32_t>(strtoul(it->second.c_str(), nullptr, 10));
    }
    // Generate sender ID if missing or zero
    if (settings.senderId == 0) {
        settings.senderId = generateSenderId();
    }

    it = map.find(KEY_NICKNAME);
    if (it != map.end() && !it->second.empty()) {
        settings.nickname = it->second.substr(0, MAX_NICKNAME_LEN);
    }

    it = map.find(KEY_CHAT_CHANNEL);
    if (it != map.end() && !it->second.empty()) {
        settings.chatChannel = it->second.substr(0, MAX_TARGET_LEN);
    }

    return settings;
}

bool saveSettings(const ChatSettingsData& settings) {
    std::map<std::string, std::string> map;

    map[KEY_SENDER_ID] = std::to_string(settings.senderId);
    map[KEY_NICKNAME] = settings.nickname;
    map[KEY_CHAT_CHANNEL] = settings.chatChannel;

    return file::savePropertiesFile(CHAT_SETTINGS_FILE, map);
}

bool settingsFileExists() {
    return access(CHAT_SETTINGS_FILE, F_OK) == 0;
}

} // namespace tt::app::chat

#endif // CONFIG_SOC_WIFI_SUPPORTED && !CONFIG_SLAVE_SOC_WIFI_SUPPORTED
