#include <Tactility/service/lxmf/Types.h>

namespace tt::service::lxmf {

const char* runtimeStateToString(RuntimeState state) {
    switch (state) {
        case RuntimeState::Starting:
            return "Starting";
        case RuntimeState::Ready:
            return "Ready";
        case RuntimeState::Stopping:
            return "Stopping";
        case RuntimeState::Stopped:
            return "Stopped";
        case RuntimeState::Faulted:
            return "Faulted";
    }
    return "?";
}

const char* deliveryStateToString(DeliveryState state) {
    switch (state) {
        case DeliveryState::Queued:
            return "Queued";
        case DeliveryState::Sending:
            return "Sending";
        case DeliveryState::Delivered:
            return "Delivered";
        case DeliveryState::Failed:
            return "Failed";
    }
    return "?";
}

const char* messageDirectionToString(MessageDirection direction) {
    switch (direction) {
        case MessageDirection::Incoming:
            return "Incoming";
        case MessageDirection::Outgoing:
            return "Outgoing";
    }
    return "?";
}

} // namespace tt::service::lxmf
