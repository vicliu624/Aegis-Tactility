#pragma once

#include <Tactility/RecursiveMutex.h>
#include <Tactility/service/reticulum/Types.h>

#include <vector>

namespace tt::service::reticulum {

class ResourceManager final {

    mutable RecursiveMutex mutex;
    std::vector<ResourceInfo> resources {};

public:

    void clear();

    bool upsertResource(const ResourceInfo& resource);

    std::vector<ResourceInfo> getResources() const;
};

} // namespace tt::service::reticulum
