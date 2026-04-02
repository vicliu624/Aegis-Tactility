#pragma once

#include <Tactility/PubSub.h>
#include <Tactility/service/ServiceContext.h>
#include <Tactility/service/lxmf/Events.h>

#include <memory>
#include <string>
#include <vector>

namespace tt::service::lxmf {

class LxmfService;

std::shared_ptr<ServiceContext> findServiceContext();

std::shared_ptr<LxmfService> findService();

std::shared_ptr<PubSub<LxmfEvent>> getPubsub();

RuntimeState getRuntimeState();

std::vector<PeerInfo> getPeers();

std::vector<ConversationInfo> getConversations();

std::vector<MessageInfo> getMessages(const reticulum::DestinationHash& peerDestination);

bool ensureConversation(
    const reticulum::DestinationHash& peerDestination,
    const std::string& title = {},
    const std::string& subtitle = {}
);

bool queueOutgoingMessage(
    const reticulum::DestinationHash& peerDestination,
    const std::string& author,
    const std::string& body
);

bool markConversationRead(const reticulum::DestinationHash& peerDestination);

} // namespace tt::service::lxmf
