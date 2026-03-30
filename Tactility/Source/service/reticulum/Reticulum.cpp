#include <Tactility/service/reticulum/Reticulum.h>

#include <Tactility/Logger.h>
#include <Tactility/service/ServiceManifest.h>
#include <Tactility/service/ServiceRegistration.h>
#include <Tactility/service/reticulum/ReticulumService.h>

namespace tt::service::reticulum {

static const auto LOGGER = Logger("Reticulum");
extern const ServiceManifest manifest;

std::shared_ptr<ServiceContext> findServiceContext() {
    return findServiceContextById(manifest.id);
}

std::shared_ptr<PubSub<ReticulumEvent>> getPubsub() {
    auto service = findService();
    return service != nullptr ? service->getPubsub() : nullptr;
}

RuntimeState getRuntimeState() {
    auto service = findService();
    if (service == nullptr) {
        LOGGER.warn("Service not running");
        return RuntimeState::Stopped;
    }
    return service->getRuntimeState();
}

bool registerInterface(const std::shared_ptr<Interface>& interfaceInstance) {
    auto service = findService();
    if (service == nullptr) {
        LOGGER.warn("Service not running");
        return false;
    }
    return service->registerInterface(interfaceInstance);
}

bool unregisterInterface(const std::string& interfaceId) {
    auto service = findService();
    if (service == nullptr) {
        LOGGER.warn("Service not running");
        return false;
    }
    return service->unregisterInterface(interfaceId);
}

std::vector<InterfaceDescriptor> getInterfaces() {
    auto service = findService();
    return service != nullptr ? service->getInterfaces() : std::vector<InterfaceDescriptor> {};
}

bool registerLocalDestination(const LocalDestination& destination) {
    auto service = findService();
    if (service == nullptr) {
        LOGGER.warn("Service not running");
        return false;
    }
    return service->registerLocalDestination(destination);
}

std::vector<RegisteredDestination> getLocalDestinations() {
    auto service = findService();
    return service != nullptr ? service->getLocalDestinations() : std::vector<RegisteredDestination> {};
}

bool registerAppEndpoint(const std::string& endpointName) {
    auto service = findService();
    if (service == nullptr) {
        LOGGER.warn("Service not running");
        return false;
    }
    return service->registerAppEndpoint(endpointName);
}

std::vector<AppEndpoint> getAppEndpoints() {
    auto service = findService();
    return service != nullptr ? service->getAppEndpoints() : std::vector<AppEndpoint> {};
}

std::vector<AnnounceInfo> getAnnounces() {
    auto service = findService();
    return service != nullptr ? service->getAnnounces() : std::vector<AnnounceInfo> {};
}

std::vector<PathEntry> getPaths() {
    auto service = findService();
    return service != nullptr ? service->getPaths() : std::vector<PathEntry> {};
}

std::vector<LinkInfo> getLinks() {
    auto service = findService();
    return service != nullptr ? service->getLinks() : std::vector<LinkInfo> {};
}

std::vector<ResourceInfo> getResources() {
    auto service = findService();
    return service != nullptr ? service->getResources() : std::vector<ResourceInfo> {};
}

bool broadcastAppData(const std::string& endpointName, const std::vector<uint8_t>& payload) {
    auto service = findService();
    if (service == nullptr) {
        LOGGER.warn("Service not running");
        return false;
    }
    return service->broadcastAppData(endpointName, payload);
}

bool sendFrame(const std::string& interfaceId, const InterfaceFrame& frame) {
    auto service = findService();
    if (service == nullptr) {
        LOGGER.warn("Service not running");
        return false;
    }
    return service->sendFrame(interfaceId, frame);
}

} // namespace tt::service::reticulum
