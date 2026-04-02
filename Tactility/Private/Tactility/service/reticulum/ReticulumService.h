#pragma once

#include <Tactility/DispatcherThread.h>
#include <Tactility/PubSub.h>
#include <Tactility/RecursiveMutex.h>
#include <Tactility/service/Service.h>
#include <Tactility/service/ServicePaths.h>
#include <Tactility/service/reticulum/Events.h>
#include <Tactility/service/reticulum/Interface.h>

#include <memory>
#include <string>
#include <vector>

namespace tt::service::reticulum {

class DestinationRegistry;
class IdentityStore;
class InterfaceManager;
class LinkManager;
class PacketCodec;
class ResourceManager;
class TransportCore;

class ReticulumService final : public Service {

    RecursiveMutex mutex;
    std::unique_ptr<ServicePaths> paths;
    RuntimeState runtimeState = RuntimeState::Stopped;
    std::shared_ptr<PubSub<ReticulumEvent>> pubsub = std::make_shared<PubSub<ReticulumEvent>>();
    std::unique_ptr<DispatcherThread> dispatcher = std::make_unique<DispatcherThread>("reticulum_dispatcher", 6144);
    std::vector<AnnounceInfo> observedAnnounces {};
    std::vector<FullHashBytes> seenPathRequestKeys {};

    std::unique_ptr<IdentityStore> identityStore;
    std::unique_ptr<DestinationRegistry> destinationRegistry;
    std::unique_ptr<PacketCodec> packetCodec;
    std::unique_ptr<TransportCore> transportCore;
    std::unique_ptr<LinkManager> linkManager;
    std::unique_ptr<ResourceManager> resourceManager;
    std::unique_ptr<InterfaceManager> interfaceManager;

    void setRuntimeState(RuntimeState newState, const char* detail = nullptr);

    void publishEvent(ReticulumEvent event);

    void observeAnnounce(AnnounceInfo announce);

    void publishPathTableChanged(const PathEntry& entry, std::string detail);

    void publishLinkTableChanged(const LinkInfo& entry, std::string detail);

    bool broadcastPacket(const std::vector<uint8_t>& packet);

    bool sendPacketOnInterface(const std::string& interfaceId, const std::vector<uint8_t>& packet);

    bool announceDestination(const RegisteredDestination& destination, bool pathResponse = false, const std::string& interfaceId = {});

    void onInboundFrame(InboundFrame frame);

public:

    ReticulumService();
    ~ReticulumService() override = default;

    bool onStart(ServiceContext& serviceContext) override;

    void onStop(ServiceContext& serviceContext) override;

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

    std::shared_ptr<PubSub<ReticulumEvent>> getPubsub() const { return pubsub; }

    RuntimeState getRuntimeState();
};

} // namespace tt::service::reticulum
