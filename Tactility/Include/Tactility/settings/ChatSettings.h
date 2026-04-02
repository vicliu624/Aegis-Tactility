#pragma once

#include <optional>
#include <string>

namespace tt::settings::chat {

struct ChatSettings {
    std::string nickname {};
};

bool load(ChatSettings& settings);
ChatSettings getDefault();
ChatSettings loadOrGetDefault();
bool save(const ChatSettings& settings);
bool validate(const ChatSettings& settings, std::optional<std::string>& error);

} // namespace tt::settings::chat
