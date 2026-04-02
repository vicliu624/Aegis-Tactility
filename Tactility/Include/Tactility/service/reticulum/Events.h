#pragma once

#include <Tactility/service/reticulum/Types.h>

#include <optional>
#include <string>

namespace tt::service::reticulum {

enum class EventType {
    RuntimeStateChanged,
    InterfaceAttached,
    InterfaceDetached,
    InterfaceStarted,
    InterfaceStopped,
    LocalDestinationRegistered,
    AnnounceObserved,
    InboundFrameQueued,
    PacketDecoded,
    PathTableChanged,
    LinkTableChanged,
    ResourceTableChanged,
    Error
};

struct ReticulumEvent {
    EventType type = EventType::Error;
    RuntimeState runtimeState = RuntimeState::Stopped;
    std::optional<InterfaceDescriptor> interface {};
    std::optional<PacketSummary> packet {};
    std::optional<AnnounceInfo> announce {};
    std::optional<PathEntry> path {};
    std::optional<LinkInfo> link {};
    std::optional<ResourceInfo> resource {};
    std::optional<DestinationHash> destination {};
    std::string detail {};
};

} // namespace tt::service::reticulum
