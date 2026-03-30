#include <Tactility/service/reticulum/PacketCodec.h>

namespace tt::service::reticulum {

std::optional<PacketSummary> PacketCodec::summarize(const std::vector<uint8_t>& packet) const {
    if (packet.size() < 2) {
        return std::nullopt;
    }

    PacketSummary summary;
    summary.flags = packet[0];
    summary.hops = packet[1];
    summary.rawSize = packet.size();
    summary.payloadSize = packet.size() > 2 ? packet.size() - 2 : 0;
    return summary;
}

} // namespace tt::service::reticulum
