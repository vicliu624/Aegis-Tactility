#pragma once

#include <Tactility/PubSub.h>
#include <Tactility/service/ServiceContext.h>
#include <Tactility/service/reticulum/Events.h>
#include <Tactility/service/reticulum/Interface.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace tt::service::reticulum {

class ReticulumService;

using RequestHandler = std::function<std::optional<std::vector<uint8_t>>(
    const RequestInfo& request,
    const std::vector<uint8_t>& requestData
)>;

using LinkMessageHandler = std::function<void(
    const LinkInfo& link,
    uint8_t context,
    const std::vector<uint8_t>& payload,
    bool viaResource,
    const std::optional<ResourceInfo>& resource
)>;

std::shared_ptr<ServiceContext> findServiceContext();

std::shared_ptr<ReticulumService> findService();

std::shared_ptr<PubSub<ReticulumEvent>> getPubsub();

RuntimeState getRuntimeState();

bool registerInterface(const std::shared_ptr<Interface>& interfaceInstance);

bool unregisterInterface(const std::string& interfaceId);

std::vector<InterfaceDescriptor> getInterfaces();

bool registerLocalDestination(const LocalDestination& destination);

std::vector<RegisteredDestination> getLocalDestinations();

bool updateLocalDestinationAppData(const DestinationHash& destinationHash, const std::vector<uint8_t>& appData);

bool announceLocalDestination(const DestinationHash& destinationHash);

bool requestPath(const DestinationHash& destinationHash, const std::vector<uint8_t>& tag = {});

bool openLink(const DestinationHash& destinationHash, DestinationHash& outLinkId);

bool sendLinkData(const DestinationHash& linkId, uint8_t context, const std::vector<uint8_t>& plaintext);

bool sendLinkResource(
    const DestinationHash& linkId,
    const std::vector<uint8_t>& plaintext,
    ResourceInfo* outResource = nullptr
);

bool identifyLink(const DestinationHash& linkId);

bool closeLink(const DestinationHash& linkId);

bool signLocalIdentity(const std::vector<uint8_t>& payload, SignatureBytes& signature);

std::optional<IdentityPublicKeyBytes> recallIdentityPublicKey(const DestinationHash& destinationHash);

bool registerRequestHandler(const DestinationHash& localDestination, const std::string& path, RequestHandler handler);

bool registerLinkHandler(const DestinationHash& localDestination, LinkMessageHandler handler);

bool sendRequest(
    const DestinationHash& linkId,
    const DestinationHash& localDestination,
    const std::string& path,
    const std::vector<uint8_t>& requestData,
    DestinationHash& outRequestId
);

std::vector<RequestInfo> getRequests();

std::vector<AnnounceInfo> getAnnounces();

std::vector<PathEntry> getPaths();

std::vector<LinkInfo> getLinks();

std::vector<ResourceInfo> getResources();

bool sendFrame(const std::string& interfaceId, const InterfaceFrame& frame);

} // namespace tt::service::reticulum
