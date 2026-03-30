#include <Tactility/service/reticulum/LinkManager.h>

namespace tt::service::reticulum {

void LinkManager::clear() {
    auto lock = mutex.asScopedLock();
    lock.lock();
    links.clear();
}

bool LinkManager::upsertLink(const LinkInfo& link) {
    auto lock = mutex.asScopedLock();
    lock.lock();

    for (auto& existing : links) {
        if (existing.linkId == link.linkId) {
            existing = link;
            return true;
        }
    }

    links.push_back(link);
    return true;
}

std::vector<LinkInfo> LinkManager::getLinks() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return links;
}

} // namespace tt::service::reticulum
