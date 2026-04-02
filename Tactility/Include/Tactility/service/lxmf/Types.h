#pragma once

#include <Tactility/service/reticulum/Types.h>

#include <cstdint>
#include <string>

namespace tt::service::lxmf {

enum class RuntimeState {
    Starting,
    Ready,
    Stopping,
    Stopped,
    Faulted
};

enum class DeliveryState {
    Queued,
    Sending,
    Delivered,
    Failed
};

enum class MessageDirection {
    Incoming,
    Outgoing
};

struct PeerInfo {
    reticulum::DestinationHash destination {};
    std::string title {};
    std::string subtitle {};
    bool reachable = false;
    uint8_t hops = 0;
};

struct ConversationInfo {
    reticulum::DestinationHash peerDestination {};
    std::string title {};
    std::string subtitle {};
    std::string preview {};
    uint32_t unreadCount = 0;
    uint32_t lastActivityTick = 0;
    DeliveryState lastDeliveryState = DeliveryState::Queued;
    bool reachable = false;
};

struct MessageInfo {
    uint64_t id = 0;
    reticulum::DestinationHash peerDestination {};
    MessageDirection direction = MessageDirection::Incoming;
    DeliveryState deliveryState = DeliveryState::Queued;
    std::string author {};
    std::string body {};
    uint32_t createdTick = 0;
    bool read = true;
};

const char* runtimeStateToString(RuntimeState state);
const char* deliveryStateToString(DeliveryState state);
const char* messageDirectionToString(MessageDirection direction);

} // namespace tt::service::lxmf
