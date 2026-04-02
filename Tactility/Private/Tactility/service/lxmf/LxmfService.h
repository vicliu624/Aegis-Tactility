#pragma once

#include <Tactility/PubSub.h>
#include <Tactility/RecursiveMutex.h>
#include <Tactility/service/Service.h>
#include <Tactility/service/ServicePaths.h>
#include <Tactility/service/lxmf/Events.h>
#include <Tactility/service/reticulum/Events.h>

#include <memory>
#include <optional>
#include <vector>

namespace tt::service::lxmf {

class LxmfService final : public Service {

    struct ConversationRecord {
        ConversationInfo summary {};
        std::vector<MessageInfo> messages {};
    };

    struct PendingDeliveryRecord {
        uint64_t messageId = 0;
        reticulum::DestinationHash peerDestination {};
        reticulum::DestinationHash linkId {};
        std::optional<reticulum::ResourceHashBytes> resourceHash {};
    };

    RecursiveMutex mutex;
    std::unique_ptr<ServicePaths> paths;
    RuntimeState runtimeState = RuntimeState::Stopped;
    std::shared_ptr<PubSub<LxmfEvent>> pubsub = std::make_shared<PubSub<LxmfEvent>>();
    std::vector<ConversationRecord> conversations {};
    std::vector<PendingDeliveryRecord> pendingDeliveries {};
    uint64_t nextMessageId = 1;
    reticulum::DestinationHash localDeliveryDestination {};
    reticulum::NameHashBytes localDeliveryNameHash {};
    PubSub<reticulum::ReticulumEvent>::SubscriptionHandle reticulumSubscription = nullptr;

    void setRuntimeState(RuntimeState newState, const char* detail = nullptr);

    void publishEvent(LxmfEvent event);

    bool loadState();

    bool persistLocked() const;

    void onReticulumEvent(const reticulum::ReticulumEvent& event);

    bool initialiseLocalDeliveryDestination();

    void processPendingDeliveries(const std::optional<reticulum::DestinationHash>& peerDestination = {});

    void processLinkUpdate(const reticulum::LinkInfo& link);

    void processResourceUpdate(const reticulum::ResourceInfo& resource);

    void processPathUpdate(const reticulum::PathEntry& path);

    void handleInboundPayload(
        const reticulum::LinkInfo& link,
        const std::vector<uint8_t>& payload,
        bool viaResource,
        const std::optional<reticulum::ResourceInfo>& resource
    );

public:

    bool onStart(ServiceContext& serviceContext) override;

    void onStop(ServiceContext& serviceContext) override;

    std::shared_ptr<PubSub<LxmfEvent>> getPubsub() const { return pubsub; }

    RuntimeState getRuntimeState();

    std::vector<PeerInfo> getPeers();

    std::vector<ConversationInfo> getConversations();

    std::vector<MessageInfo> getMessages(const reticulum::DestinationHash& peerDestination);

    bool refreshLocalPeerProfile();

    bool ensureConversation(
        const reticulum::DestinationHash& peerDestination,
        const std::string& title,
        const std::string& subtitle
    );

    bool queueOutgoingMessage(
        const reticulum::DestinationHash& peerDestination,
        const std::string& author,
        const std::string& body
    );

    bool markConversationRead(const reticulum::DestinationHash& peerDestination);
};

} // namespace tt::service::lxmf
