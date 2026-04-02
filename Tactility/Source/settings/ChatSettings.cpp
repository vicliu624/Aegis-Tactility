#include <Tactility/settings/ChatSettings.h>

#include <Tactility/file/PropertiesFile.h>

#include <map>

namespace tt::settings::chat {

constexpr auto* SETTINGS_FILE = "/data/settings/chat.properties";
constexpr auto* KEY_NICKNAME = "nickname";
constexpr size_t MAX_NICKNAME_LENGTH = 64;

ChatSettings getDefault() {
    return ChatSettings {
        .nickname = {}
    };
}

bool load(ChatSettings& settings) {
    std::map<std::string, std::string> map;
    if (!file::loadPropertiesFile(SETTINGS_FILE, map)) {
        return false;
    }

    settings = getDefault();

    if (const auto it = map.find(KEY_NICKNAME); it != map.end() && !it->second.empty()) {
        settings.nickname = it->second;
    }

    std::optional<std::string> error;
    return validate(settings, error);
}

ChatSettings loadOrGetDefault() {
    ChatSettings settings;
    if (!load(settings)) {
        settings = getDefault();
    }
    return settings;
}

bool save(const ChatSettings& settings) {
    std::optional<std::string> error;
    if (!validate(settings, error)) {
        return false;
    }

    std::map<std::string, std::string> map;
    map[KEY_NICKNAME] = settings.nickname;
    return file::savePropertiesFile(SETTINGS_FILE, map);
}

bool validate(const ChatSettings& settings, std::optional<std::string>& error) {
    error.reset();

    if (settings.nickname.empty()) {
        error = "Nickname cannot be empty";
        return false;
    }

    if (settings.nickname.size() > MAX_NICKNAME_LENGTH) {
        error = "Nickname must be at most 64 characters";
        return false;
    }

    return true;
}

} // namespace tt::settings::chat
