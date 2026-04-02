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

bool updateLocalDestinationAppData(const DestinationHash& destinationHash, const std::vector<uint8_t>& appData) {
    const auto service = findService();
    if (service == nullptr) {
        LOGGER.warn("Service not running");
        return false;
    }

    return service->updateLocalDestinationAppData(destinationHash, appData);
}

bool announceLocalDestination(const DestinationHash& destinationHash) {
    auto service = findService();
    if (service == nullptr) {
        LOGGER.warn("Service not running");
        return false;
    }
    return service->announceLocalDestination(destinationHash);
}

bool requestPath(const DestinationHash& destinationHash, const std::vector<uint8_t>& tag) {
    auto service = findService();
    if (service == nullptr) {
        LOGGER.warn("Service not running");
        return false;
    }
    return service->requestPath(destinationHash, tag);
}

bool openLink(const DestinationHash& destinationHash, DestinationHash& outLinkId) {
    auto service = findService();
    if (service == nullptr) {
        LOGGER.warn("Service not running");
        return false;
    }
    return service->openLink(destinationHash, outLinkId);
}

bool sendLinkData(const DestinationHash& linkId, uint8_t context, const std::vector<uint8_t>& plaintext) {
    auto service = findService();
    if (service == nullptr) {
        LOGGER.warn("Service not running");
        return false;
    }
    return service->sendLinkData(linkId, context, plaintext);
}

bool sendLinkResource(
    const DestinationHash& linkId,
    const std::vector<uint8_t>& plaintext,
    ResourceInfo* outResource
) {
    const auto service = findService();
    if (service == nullptr) {
        LOGGER.warn("Service not running");
        return false;
    }

    return service->sendLinkResource(linkId, plaintext, outResource);
}

bool identifyLink(const DestinationHash& linkId) {
    auto service = findService();
    if (service == nullptr) {
        LOGGER.warn("Service not running");
        return false;
    }
    return service->identifyLink(linkId);
}

bool closeLink(const DestinationHash& linkId) {
    auto service = findService();
    if (service == nullptr) {
        LOGGER.warn("Service not running");
        return false;
    }
    return service->closeLink(linkId);
}

bool signLocalIdentity(const std::vector<uint8_t>& payload, SignatureBytes& signature) {
    const auto service = findService();
    if (service == nullptr) {
        LOGGER.warn("Reticulum service unavailable while signing local identity payload");
        return false;
    }

    return service->signLocalIdentity(payload, signature);
}

std::optional<IdentityPublicKeyBytes> recallIdentityPublicKey(const DestinationHash& destinationHash) {
    if (const auto service = findService(); service != nullptr) {
        return service->recallIdentityPublicKey(destinationHash);
    }

    return std::nullopt;
}

bool registerRequestHandler(const DestinationHash& localDestination, const std::string& path, RequestHandler handler) {
    const auto service = findService();
    if (service == nullptr) {
        LOGGER.warn("Reticulum service unavailable while registering request handler {}", path);
        return false;
    }

    return service->registerRequestHandler(localDestination, path, std::move(handler));
}

bool registerLinkHandler(const DestinationHash& localDestination, LinkMessageHandler handler) {
    const auto service = findService();
    if (service == nullptr) {
        LOGGER.warn("Reticulum service unavailable while registering link handler");
        return false;
    }

    return service->registerLinkHandler(localDestination, std::move(handler));
}

bool sendRequest(
    const DestinationHash& linkId,
    const DestinationHash& localDestination,
    const std::string& path,
    const std::vector<uint8_t>& requestData,
    DestinationHash& outRequestId
) {
    const auto service = findService();
    if (service == nullptr) {
        LOGGER.warn("Reticulum service unavailable while sending request {}", path);
        return false;
    }

    return service->sendRequest(linkId, localDestination, path, requestData, outRequestId);
}

std::vector<RequestInfo> getRequests() {
    if (const auto service = findService(); service != nullptr) {
        return service->getRequests();
    }

    return {};
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

bool sendFrame(const std::string& interfaceId, const InterfaceFrame& frame) {
    auto service = findService();
    if (service == nullptr) {
        LOGGER.warn("Service not running");
        return false;
    }
    return service->sendFrame(interfaceId, frame);
}

} // namespace tt::service::reticulum
