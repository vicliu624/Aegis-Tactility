#include <Tactility/service/reticulum/ResourceManager.h>

#include <algorithm>

namespace tt::service::reticulum {

void ResourceManager::clear() {
    auto lock = mutex.asScopedLock();
    lock.lock();
    resources.clear();
}

bool ResourceManager::upsertResource(const ResourceInfo& resource) {
    auto lock = mutex.asScopedLock();
    lock.lock();

    for (auto& existing : resources) {
        if (existing.resourceHash == resource.resourceHash) {
            existing = resource;
            return true;
        }
    }

    resources.push_back(resource);
    return true;
}

std::vector<ResourceInfo> ResourceManager::getResources() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return resources;
}

} // namespace tt::service::reticulum
