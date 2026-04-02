#pragma once

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && !defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include <Tactility/RecursiveMutex.h>
#include <Tactility/service/lxmf/Types.h>

#include <string>
#include <vector>

namespace tt::app::chat {

enum class ScreenMode {
    Conversations,
    Contacts,
    Thread
};

class ChatState {

    mutable RecursiveMutex mutex;

    ScreenMode screenMode = ScreenMode::Conversations;
    std::vector<service::lxmf::ConversationInfo> conversations {};
    std::vector<service::lxmf::PeerInfo> peers {};
    std::vector<service::lxmf::MessageInfo> messages {};
    std::string localNickname {};
    service::reticulum::DestinationHash activePeer {};
    std::string activeTitle {};
    bool hasActivePeer = false;

public:
    ChatState() = default;
    ~ChatState() = default;

    ChatState(const ChatState&) = delete;
    ChatState& operator=(const ChatState&) = delete;
    ChatState(ChatState&&) = delete;
    ChatState& operator=(ChatState&&) = delete;

    void setLocalNickname(const std::string& nickname);
    std::string getLocalNickname() const;

    void showConversations();
    void showContacts();
    void showThread(
        const service::reticulum::DestinationHash& peerDestination,
        const std::string& title
    );

    ScreenMode getScreenMode() const;

    void setConversations(std::vector<service::lxmf::ConversationInfo> items);
    std::vector<service::lxmf::ConversationInfo> getConversations() const;

    void setPeers(std::vector<service::lxmf::PeerInfo> items);
    std::vector<service::lxmf::PeerInfo> getPeers() const;

    void setMessages(std::vector<service::lxmf::MessageInfo> items);
    std::vector<service::lxmf::MessageInfo> getMessages() const;

    bool getActivePeer(service::reticulum::DestinationHash& outDestination) const;
    std::string getActiveTitle() const;
};

} // namespace tt::app::chat

#endif // CONFIG_SOC_WIFI_SUPPORTED && !CONFIG_SLAVE_SOC_WIFI_SUPPORTED
