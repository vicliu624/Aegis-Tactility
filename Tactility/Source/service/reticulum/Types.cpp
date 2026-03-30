#include <Tactility/service/reticulum/Types.h>

#include <algorithm>
#include <format>

namespace tt::service::reticulum {

bool DestinationHash::empty() const {
    return std::all_of(bytes.begin(), bytes.end(), [](uint8_t value) {
        return value == 0;
    });
}

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

const char* interfaceKindToString(InterfaceKind kind) {
    switch (kind) {
        case InterfaceKind::Unknown:
            return "Unknown";
        case InterfaceKind::EspNow:
            return "EspNow";
        case InterfaceKind::LoRa:
            return "LoRa";
    }
    return "?";
}

const char* linkStateToString(LinkState state) {
    switch (state) {
        case LinkState::Pending:
            return "Pending";
        case LinkState::Handshake:
            return "Handshake";
        case LinkState::Active:
            return "Active";
        case LinkState::Stale:
            return "Stale";
        case LinkState::Closed:
            return "Closed";
    }
    return "?";
}

const char* resourceStateToString(ResourceState state) {
    switch (state) {
        case ResourceState::None:
            return "None";
        case ResourceState::Queued:
            return "Queued";
        case ResourceState::Advertised:
            return "Advertised";
        case ResourceState::Transferring:
            return "Transferring";
        case ResourceState::AwaitingProof:
            return "AwaitingProof";
        case ResourceState::Assembling:
            return "Assembling";
        case ResourceState::Complete:
            return "Complete";
        case ResourceState::Failed:
            return "Failed";
        case ResourceState::Corrupt:
            return "Corrupt";
        case ResourceState::Rejected:
            return "Rejected";
    }
    return "?";
}

template<typename Container>
static std::string bytesToHex(const Container& bytes) {
    std::string output;
    output.reserve(bytes.size() * 2);
    for (const auto value : bytes) {
        output += std::format("{:02x}", value);
    }
    return output;
}

std::string toHex(const DestinationHash& hash) {
    return bytesToHex(hash.bytes);
}

std::string toHex(const ResourceHashBytes& hash) {
    return bytesToHex(hash);
}

} // namespace tt::service::reticulum
