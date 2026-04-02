#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && !defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include <Tactility/app/chat/ChatState.h>

namespace tt::app::chat {

void ChatState::setLocalNickname(const std::string& nickname) {
    auto lock = mutex.asScopedLock();
    lock.lock();
    localNickname = nickname;
}

std::string ChatState::getLocalNickname() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return localNickname;
}

void ChatState::showConversations() {
    auto lock = mutex.asScopedLock();
    lock.lock();
    screenMode = ScreenMode::Conversations;
}

void ChatState::showContacts() {
    auto lock = mutex.asScopedLock();
    lock.lock();
    screenMode = ScreenMode::Contacts;
}

void ChatState::showThread(
    const service::reticulum::DestinationHash& peerDestination,
    const std::string& title
) {
    auto lock = mutex.asScopedLock();
    lock.lock();
    screenMode = ScreenMode::Thread;
    activePeer = peerDestination;
    activeTitle = title;
    hasActivePeer = true;
}

ScreenMode ChatState::getScreenMode() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return screenMode;
}

void ChatState::setConversations(std::vector<service::lxmf::ConversationInfo> items) {
    auto lock = mutex.asScopedLock();
    lock.lock();
    conversations = std::move(items);
}

std::vector<service::lxmf::ConversationInfo> ChatState::getConversations() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return conversations;
}

void ChatState::setPeers(std::vector<service::lxmf::PeerInfo> items) {
    auto lock = mutex.asScopedLock();
    lock.lock();
    peers = std::move(items);
}

std::vector<service::lxmf::PeerInfo> ChatState::getPeers() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return peers;
}

void ChatState::setMessages(std::vector<service::lxmf::MessageInfo> items) {
    auto lock = mutex.asScopedLock();
    lock.lock();
    messages = std::move(items);
}

std::vector<service::lxmf::MessageInfo> ChatState::getMessages() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return messages;
}

bool ChatState::getActivePeer(service::reticulum::DestinationHash& outDestination) const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    if (!hasActivePeer) {
        return false;
    }
    outDestination = activePeer;
    return true;
}

void ChatState::setActiveTitle(const std::string& title) {
    auto lock = mutex.asScopedLock();
    lock.lock();
    activeTitle = title;
}

std::string ChatState::getActiveTitle() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return activeTitle;
}

} // namespace tt::app::chat

#endif // CONFIG_SOC_WIFI_SUPPORTED && !CONFIG_SLAVE_SOC_WIFI_SUPPORTED
