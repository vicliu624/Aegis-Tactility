#pragma once

#include <Tactility/RecursiveMutex.h>
#include <Tactility/service/reticulum/Types.h>

#include <vector>

namespace tt::service::reticulum {

class LinkManager final {

    mutable RecursiveMutex mutex;
    std::vector<LinkInfo> links {};

public:

    void clear();

    bool upsertLink(const LinkInfo& link);

    std::vector<LinkInfo> getLinks() const;
};

} // namespace tt::service::reticulum
