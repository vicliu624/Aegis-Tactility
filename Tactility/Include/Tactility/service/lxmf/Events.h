#pragma once

#include <Tactility/service/lxmf/Types.h>

#include <optional>
#include <string>

namespace tt::service::lxmf {

enum class EventType {
    RuntimeStateChanged,
    PeerDirectoryChanged,
    ConversationListChanged,
    MessageListChanged,
    Error
};

struct LxmfEvent {
    EventType type = EventType::Error;
    RuntimeState runtimeState = RuntimeState::Stopped;
    std::optional<PeerInfo> peer {};
    std::optional<ConversationInfo> conversation {};
    std::optional<MessageInfo> message {};
    std::optional<reticulum::DestinationHash> destination {};
    std::string detail {};
};

} // namespace tt::service::lxmf
