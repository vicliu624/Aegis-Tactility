#include <Tactility/service/reticulum/InterfaceManager.h>

namespace tt::service::reticulum {

bool InterfaceManager::addInterface(const std::shared_ptr<Interface>& interfaceInstance, ReceiveCallback receiveCallback) {
    if (interfaceInstance == nullptr) {
        return false;
    }

    auto lock = mutex.asScopedLock();
    lock.lock();

    const auto id = interfaceInstance->getId();
    if (records.contains(id)) {
        return false;
    }

    interfaceInstance->setReceiveCallback(std::move(receiveCallback));
    const bool started = interfaceInstance->start();

    records[id] = Record {
        .interfaceInstance = interfaceInstance,
        .descriptor = InterfaceDescriptor {
            .id = id,
            .kind = interfaceInstance->getKind(),
            .capabilities = interfaceInstance->getCapabilities(),
            .started = started,
            .metrics = interfaceInstance->getMetrics()
        }
    };

    return true;
}

bool InterfaceManager::removeInterface(const std::string& interfaceId) {
    auto lock = mutex.asScopedLock();
    lock.lock();

    const auto iterator = records.find(interfaceId);
    if (iterator == records.end()) {
        return false;
    }

    iterator->second.interfaceInstance->stop();
    records.erase(iterator);
    return true;
}

void InterfaceManager::stopAll() {
    auto lock = mutex.asScopedLock();
    lock.lock();

    for (auto& [id, record] : records) {
        record.interfaceInstance->stop();
        record.descriptor.started = false;
        record.descriptor.metrics = record.interfaceInstance->getMetrics();
    }
}

std::vector<InterfaceDescriptor> InterfaceManager::getInterfaces() const {
    auto lock = mutex.asScopedLock();
    lock.lock();

    std::vector<InterfaceDescriptor> interfaces;
    interfaces.reserve(records.size());
    for (const auto& [id, record] : records) {
        auto descriptor = record.descriptor;
        descriptor.metrics = record.interfaceInstance->getMetrics();
        interfaces.push_back(descriptor);
    }
    return interfaces;
}

bool InterfaceManager::sendFrame(const std::string& interfaceId, const InterfaceFrame& frame) {
    auto lock = mutex.asScopedLock();
    lock.lock();

    const auto iterator = records.find(interfaceId);
    if (iterator == records.end()) {
        return false;
    }

    return iterator->second.interfaceInstance->sendFrame(frame);
}

} // namespace tt::service::reticulum
