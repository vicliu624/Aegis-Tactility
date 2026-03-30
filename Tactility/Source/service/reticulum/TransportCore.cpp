#include <Tactility/service/reticulum/TransportCore.h>

#include <algorithm>

namespace tt::service::reticulum {

void TransportCore::clear() {
    auto lock = mutex.asScopedLock();
    lock.lock();
    paths.clear();
}

bool TransportCore::installPath(const PathEntry& entry) {
    auto lock = mutex.asScopedLock();
    lock.lock();

    for (auto& existing : paths) {
        if (existing.destination == entry.destination) {
            existing = entry;
            return true;
        }
    }

    paths.push_back(entry);
    return true;
}

bool TransportCore::removePath(const DestinationHash& destinationHash) {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return std::erase_if(paths, [&destinationHash](const auto& entry) {
        return entry.destination == destinationHash;
    }) > 0;
}

std::vector<PathEntry> TransportCore::getPaths() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return paths;
}

} // namespace tt::service::reticulum
