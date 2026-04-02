#pragma once

#include <Tactility/PubSub.h>
#include <Tactility/service/ServiceContext.h>
#include <Tactility/service/reticulum/Events.h>
#include <Tactility/service/reticulum/Interface.h>

#include <memory>
#include <string>
#include <vector>

namespace tt::service::reticulum {

class ReticulumService;

std::shared_ptr<ServiceContext> findServiceContext();

std::shared_ptr<ReticulumService> findService();

std::shared_ptr<PubSub<ReticulumEvent>> getPubsub();

RuntimeState getRuntimeState();

bool registerInterface(const std::shared_ptr<Interface>& interfaceInstance);

bool unregisterInterface(const std::string& interfaceId);

std::vector<InterfaceDescriptor> getInterfaces();

bool registerLocalDestination(const LocalDestination& destination);

std::vector<RegisteredDestination> getLocalDestinations();

bool announceLocalDestination(const DestinationHash& destinationHash);

bool requestPath(const DestinationHash& destinationHash, const std::vector<uint8_t>& tag = {});

bool openLink(const DestinationHash& destinationHash, DestinationHash& outLinkId);

bool sendLinkData(const DestinationHash& linkId, uint8_t context, const std::vector<uint8_t>& plaintext);

bool identifyLink(const DestinationHash& linkId);

bool closeLink(const DestinationHash& linkId);

std::vector<AnnounceInfo> getAnnounces();

std::vector<PathEntry> getPaths();

std::vector<LinkInfo> getLinks();

std::vector<ResourceInfo> getResources();

bool sendFrame(const std::string& interfaceId, const InterfaceFrame& frame);

} // namespace tt::service::reticulum
