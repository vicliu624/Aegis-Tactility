#include <Tactility/service/lxmf/Lxmf.h>

#include <Tactility/Logger.h>
#include <Tactility/service/ServiceManifest.h>
#include <Tactility/service/ServiceRegistration.h>
#include <Tactility/service/lxmf/LxmfService.h>

namespace tt::service::lxmf {

static const auto LOGGER = Logger("LXMF");
extern const ServiceManifest manifest;

std::shared_ptr<ServiceContext> findServiceContext() {
    return findServiceContextById(manifest.id);
}

std::shared_ptr<PubSub<LxmfEvent>> getPubsub() {
    const auto service = findService();
    return service != nullptr ? service->getPubsub() : nullptr;
}

RuntimeState getRuntimeState() {
    const auto service = findService();
    if (service == nullptr) {
        LOGGER.warn("Service not running");
        return RuntimeState::Stopped;
    }
    return service->getRuntimeState();
}

std::vector<PeerInfo> getPeers() {
    const auto service = findService();
    return service != nullptr ? service->getPeers() : std::vector<PeerInfo> {};
}

std::vector<ConversationInfo> getConversations() {
    const auto service = findService();
    return service != nullptr ? service->getConversations() : std::vector<ConversationInfo> {};
}

std::vector<MessageInfo> getMessages(const reticulum::DestinationHash& peerDestination) {
    const auto service = findService();
    return service != nullptr ? service->getMessages(peerDestination) : std::vector<MessageInfo> {};
}

bool refreshLocalPeerProfile() {
    const auto service = findService();
    if (service == nullptr) {
        LOGGER.warn("Service not running");
        return false;
    }

    return service->refreshLocalPeerProfile();
}

bool ensureConversation(
    const reticulum::DestinationHash& peerDestination,
    const std::string& title,
    const std::string& subtitle
) {
    const auto service = findService();
    if (service == nullptr) {
        LOGGER.warn("Service not running");
        return false;
    }
    return service->ensureConversation(peerDestination, title, subtitle);
}

bool queueOutgoingMessage(
    const reticulum::DestinationHash& peerDestination,
    const std::string& author,
    const std::string& body
) {
    const auto service = findService();
    if (service == nullptr) {
        LOGGER.warn("Service not running");
        return false;
    }
    return service->queueOutgoingMessage(peerDestination, author, body);
}

bool markConversationRead(const reticulum::DestinationHash& peerDestination) {
    const auto service = findService();
    if (service == nullptr) {
        LOGGER.warn("Service not running");
        return false;
    }
    return service->markConversationRead(peerDestination);
}

std::shared_ptr<LxmfService> findService() {
    return service::findServiceById<LxmfService>(manifest.id);
}

extern const ServiceManifest manifest = {
    .id = "LXMF",
    .createService = create<LxmfService>
};

} // namespace tt::service::lxmf
