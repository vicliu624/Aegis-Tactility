#include <Tactility/service/reticulum/Types.h>

#include <algorithm>
#include <cctype>

namespace tt::service::reticulum {

namespace {

template<typename Container>
bool isAllZero(const Container& bytes) {
    return std::all_of(bytes.begin(), bytes.end(), [](uint8_t value) {
        return value == 0;
    });
}

int hexNibble(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return 10 + (ch - 'a');
    }
    if (ch >= 'A' && ch <= 'F') {
        return 10 + (ch - 'A');
    }
    return -1;
}

} // namespace

bool DestinationHash::empty() const {
    return isAllZero(bytes);
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

const char* headerTypeToString(HeaderType type) {
    switch (type) {
        case HeaderType::Header1:
            return "HEADER_1";
        case HeaderType::Header2:
            return "HEADER_2";
    }
    return "?";
}

const char* transportTypeToString(TransportType type) {
    switch (type) {
        case TransportType::Broadcast:
            return "BROADCAST";
        case TransportType::Transport:
            return "TRANSPORT";
    }
    return "?";
}

const char* packetTypeToString(PacketType type) {
    switch (type) {
        case PacketType::Data:
            return "DATA";
        case PacketType::Announce:
            return "ANNOUNCE";
        case PacketType::LinkRequest:
            return "LINKREQUEST";
        case PacketType::Proof:
            return "PROOF";
    }
    return "?";
}

const char* destinationTypeToString(DestinationType type) {
    switch (type) {
        case DestinationType::Single:
            return "SINGLE";
        case DestinationType::Group:
            return "GROUP";
        case DestinationType::Plain:
            return "PLAIN";
        case DestinationType::Link:
            return "LINK";
    }
    return "?";
}

const char* linkModeToString(LinkMode mode) {
    switch (mode) {
        case LinkMode::Aes128Cbc:
            return "AES_128_CBC";
        case LinkMode::Aes256Cbc:
            return "AES_256_CBC";
        case LinkMode::Aes256Gcm:
            return "AES_256_GCM";
        case LinkMode::Unknown:
            return "UNKNOWN";
    }
    return "?";
}

const char* packetContextToString(uint8_t context) {
    switch (static_cast<PacketContext>(context)) {
        case PacketContext::None:
            return "NONE";
        case PacketContext::Resource:
            return "RESOURCE";
        case PacketContext::ResourceAdv:
            return "RESOURCE_ADV";
        case PacketContext::ResourceReq:
            return "RESOURCE_REQ";
        case PacketContext::ResourceHmu:
            return "RESOURCE_HMU";
        case PacketContext::ResourcePrf:
            return "RESOURCE_PRF";
        case PacketContext::ResourceIcl:
            return "RESOURCE_ICL";
        case PacketContext::ResourceRcl:
            return "RESOURCE_RCL";
        case PacketContext::CacheRequest:
            return "CACHE_REQUEST";
        case PacketContext::Request:
            return "REQUEST";
        case PacketContext::Response:
            return "RESPONSE";
        case PacketContext::PathResponse:
            return "PATH_RESPONSE";
        case PacketContext::Command:
            return "COMMAND";
        case PacketContext::CommandStatus:
            return "COMMAND_STATUS";
        case PacketContext::Channel:
            return "CHANNEL";
        case PacketContext::Keepalive:
            return "KEEPALIVE";
        case PacketContext::LinkIdentify:
            return "LINKIDENTIFY";
        case PacketContext::LinkClose:
            return "LINKCLOSE";
        case PacketContext::LinkProof:
            return "LINKPROOF";
        case PacketContext::LrRtt:
            return "LRRTT";
        case PacketContext::LrProof:
            return "LRPROOF";
    }
    return "?";
}

std::string toHex(const DestinationHash& hash) {
    return toHex(hash.bytes);
}

bool parseHex(std::string_view text, uint8_t* output, size_t outputSize) {
    if (text.size() != outputSize * 2) {
        return false;
    }

    for (size_t index = 0; index < outputSize; index++) {
        const auto high = hexNibble(text[index * 2]);
        const auto low = hexNibble(text[index * 2 + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        output[index] = static_cast<uint8_t>((high << 4) | low);
    }

    return true;
}

} // namespace tt::service::reticulum
