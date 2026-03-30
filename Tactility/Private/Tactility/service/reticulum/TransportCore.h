#pragma once

#include <Tactility/RecursiveMutex.h>
#include <Tactility/service/reticulum/Types.h>

#include <optional>
#include <vector>

namespace tt::service::reticulum {

class TransportCore final {

    mutable RecursiveMutex mutex;
    std::vector<PathEntry> paths {};

public:

    void clear();

    bool installPath(const PathEntry& entry);

    bool removePath(const DestinationHash& destinationHash);

    std::optional<PathEntry> getPath(const DestinationHash& destinationHash) const;

    std::vector<PathEntry> getPaths() const;
};

} // namespace tt::service::reticulum
