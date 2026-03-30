#pragma once

#include <Tactility/service/reticulum/Types.h>

#include <optional>
#include <vector>

namespace tt::service::reticulum {

class PacketCodec final {

public:

    std::optional<PacketSummary> summarize(const std::vector<uint8_t>& packet) const;
};

} // namespace tt::service::reticulum
