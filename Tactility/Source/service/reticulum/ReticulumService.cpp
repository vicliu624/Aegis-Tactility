#include <Tactility/service/reticulum/Reticulum.h>

#include <Tactility/Logger.h>
#include <Tactility/file/File.h>
#include <Tactility/kernel/Kernel.h>
#include <Tactility/service/ServiceManifest.h>
#include <Tactility/service/ServiceRegistration.h>
#include <Tactility/service/reticulum/Crypto.h>
#include <Tactility/service/reticulum/DestinationRegistry.h>
#include <Tactility/service/reticulum/IdentityStore.h>
#include <Tactility/service/reticulum/InterfaceManager.h>
#include <Tactility/service/reticulum/LinkManager.h>
#include <Tactility/service/reticulum/PacketCodec.h>
#include <Tactility/service/reticulum/ResourceManager.h>
#include <Tactility/service/reticulum/ReticulumService.h>
#include <Tactility/service/reticulum/TransportCore.h>
#include <Tactility/service/reticulum/interfaces/EspNowInterface.h>
#include <Tactility/service/reticulum/interfaces/LoRaInterface.h>
#include <Tactility/settings/ReticulumSettings.h>

#include <algorithm>
#include <format>

namespace tt::service::reticulum {

extern const ServiceManifest manifest;

namespace {

static const auto LOGGER = Logger("Reticulum");
constexpr uint32_t PATH_EXPIRY_SECONDS = 60U * 60U * 24U * 7U;
constexpr size_t MAX_DISCOVERY_TAGS = 512;
constexpr uint8_t KEEPALIVE_REQUEST = 0xFF;
constexpr uint8_t KEEPALIVE_REPLY = 0xFE;

InterfaceDescriptor toDescriptor(const InboundFrame& frame) {
    return InterfaceDescriptor {
        .id = frame.interfaceId,
        .kind = frame.interfaceKind,
        .capabilities = toMask(InterfaceCapability::None),
        .started = true,
        .metrics = frame.metrics
    };
}

bool shouldInstallPath(const std::optional<PathEntry>& existing, const PathEntry& candidate) {
    if (!existing.has_value()) {
        return true;
    }

    const auto now = kernel::getTicks();
    if (existing->expiryTick <= now) {
        return true;
    }

    if (candidate.hops < existing->hops) {
        return true;
    }

    if (candidate.announceRandom != existing->announceRandom) {
        return true;
    }

    return existing->unresponsive;
}

std::vector<uint8_t> buildDiscoveryTag(const std::vector<uint8_t>& preferredTag) {
    if (!preferredTag.empty()) {
        const auto size = std::min(preferredTag.size(), PATH_REQUEST_TAG_MAX_LENGTH);
        return std::vector<uint8_t>(preferredTag.begin(), preferredTag.begin() + size);
    }

    std::vector<uint8_t> tag(PATH_REQUEST_TAG_MAX_LENGTH, 0);
    if (crypto::fillRandom(tag.data(), tag.size())) {
        return tag;
    }

    const auto ticks = kernel::getTicks();
    for (size_t index = 0; index < tag.size(); index++) {
        tag[index] = static_cast<uint8_t>((ticks >> ((index % sizeof(ticks)) * 8)) & 0xFF);
    }
    return tag;
}

} // namespace

ReticulumService::ReticulumService() :
    identityStore(std::make_unique<IdentityStore>()),
    destinationRegistry(std::make_unique<DestinationRegistry>()),
    packetCodec(std::make_unique<PacketCodec>()),
    transportCore(std::make_unique<TransportCore>()),
    linkManager(std::make_unique<LinkManager>()),
    resourceManager(std::make_unique<ResourceManager>()),
    interfaceManager(std::make_unique<InterfaceManager>())
{}

void ReticulumService::setRuntimeState(RuntimeState newState, const char* detail) {
    {
        auto lock = mutex.asScopedLock();
        lock.lock();
        runtimeState = newState;
    }

    publishEvent(ReticulumEvent {
        .type = EventType::RuntimeStateChanged,
        .runtimeState = newState,
        .detail = detail != nullptr ? detail : runtimeStateToString(newState)
    });
}

void ReticulumService::publishEvent(ReticulumEvent event) {
    pubsub->publish(std::move(event));
}

void ReticulumService::observeAnnounce(AnnounceInfo announce) {
    {
        auto lock = mutex.asScopedLock();
        lock.lock();

        const auto iterator = std::find_if(observedAnnounces.begin(), observedAnnounces.end(), [&announce](const auto& existing) {
            return existing.destination == announce.destination;
        });

        if (iterator != observedAnnounces.end()) {
            *iterator = announce;
        } else {
            observedAnnounces.push_back(announce);
        }
    }

    publishEvent(ReticulumEvent {
        .type = EventType::AnnounceObserved,
        .runtimeState = getRuntimeState(),
        .announce = announce,
        .destination = announce.destination,
        .detail = announce.local
            ? std::format("Local announce ready for {}", toHex(announce.destination))
            : std::format(
                "Observed {} announce for {} via {}",
                announce.pathResponse ? "path-response" : "network",
                toHex(announce.destination),
                announce.interfaceId.empty() ? "unknown interface" : announce.interfaceId
            )
    });
}

void ReticulumService::publishPathTableChanged(const PathEntry& entry, std::string detail) {
    publishEvent(ReticulumEvent {
        .type = EventType::PathTableChanged,
        .runtimeState = getRuntimeState(),
        .path = entry,
        .destination = entry.destination,
        .detail = detail.empty() ? std::format("Path updated for {}", toHex(entry.destination)) : std::move(detail)
    });
}

void ReticulumService::publishLinkTableChanged(const LinkInfo& entry, std::string detail) {
    publishEvent(ReticulumEvent {
        .type = EventType::LinkTableChanged,
        .runtimeState = getRuntimeState(),
        .link = entry,
        .destination = entry.linkId,
        .detail = detail.empty() ? std::format("Link state changed for {}", toHex(entry.linkId)) : std::move(detail)
    });
}

bool ReticulumService::broadcastPacket(const std::vector<uint8_t>& packet) {
    bool delivered = false;
    for (const auto& descriptor : interfaceManager->getInterfaces()) {
        if (!descriptor.started || !hasCapability(descriptor.capabilities, InterfaceCapability::Broadcast)) {
            continue;
        }

        delivered = interfaceManager->sendFrame(descriptor.id, InterfaceFrame {
            .payload = packet,
            .broadcast = true
        }) || delivered;
    }
    return delivered;
}

bool ReticulumService::sendPacketOnInterface(const std::string& interfaceId, const std::vector<uint8_t>& packet) {
    if (interfaceId.empty()) {
        return false;
    }

    return interfaceManager->sendFrame(interfaceId, InterfaceFrame {
        .payload = packet,
        .broadcast = false
    });
}

bool ReticulumService::announceDestination(const RegisteredDestination& destination, bool pathResponse, const std::string& interfaceId) {
    const auto localIdentity = identityStore->getLocalIdentity();
    const auto packet = packetCodec->encodeAnnounce(destination, localIdentity.signingPrivateKey, pathResponse);
    if (packet.empty()) {
        return false;
    }

    const bool delivered = interfaceId.empty()
        ? broadcastPacket(packet)
        : sendPacketOnInterface(interfaceId, packet);
    if (!delivered) {
        return false;
    }

    const auto summary = packetCodec->summarize(packet);
    observeAnnounce(AnnounceInfo {
        .destination = destination.hash,
        .identityHash = destination.identityHash,
        .nameHash = destination.nameHash,
        .identityPublicKey = destination.identityPublicKey,
        .packetHash = summary.has_value() ? summary->packetHash : FullHashBytes {},
        .interfaceId = interfaceId,
        .interfaceKind = InterfaceKind::Unknown,
        .appData = destination.appData,
        .hops = 0,
        .context = static_cast<uint8_t>(pathResponse ? PacketContext::PathResponse : PacketContext::None),
        .local = true,
        .pathResponse = pathResponse,
        .validated = true,
        .observedTick = kernel::getTicks()
    });
    return true;
}

bool ReticulumService::onStart(ServiceContext& serviceContext) {
    LOGGER.info("Starting Reticulum service");

    paths = serviceContext.getPaths();
    if (paths == nullptr) {
        LOGGER.error("No service paths available");
        setRuntimeState(RuntimeState::Faulted, "No service paths available");
        return false;
    }

    setRuntimeState(RuntimeState::Starting);

    if (!file::findOrCreateDirectory(paths->getUserDataDirectory(), 0777)) {
        LOGGER.error("Failed to create {}", paths->getUserDataDirectory());
        setRuntimeState(RuntimeState::Faulted, "Failed to create user data directory");
        return false;
    }

    for (const auto* child : { "identities", "ratchets", "paths", "links", "resources", "interfaces", "cache" }) {
        const auto path = paths->getUserDataPath(child);
        if (!file::findOrCreateDirectory(path, 0777)) {
            LOGGER.error("Failed to create {}", path);
            setRuntimeState(RuntimeState::Faulted, "Failed to create reticulum subdirectory");
            return false;
        }
    }

    if (!identityStore->init(*paths)) {
        LOGGER.error("Identity store init failed");
        setRuntimeState(RuntimeState::Faulted, "Identity store init failed");
        return false;
    }

    dispatcher->start();

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && !defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)
    registerInterface(std::make_shared<interfaces::EspNowInterface>());
#endif

    const auto loRaSettings = settings::reticulum::loadOrGetDefault();
    if (loRaSettings.enabled) {
        registerInterface(std::make_shared<interfaces::LoRaInterface>(loRaSettings));
    }

    setRuntimeState(RuntimeState::Ready);
    LOGGER.info("Reticulum service ready at {}", paths->getUserDataDirectory());
    return true;
}

void ReticulumService::onStop(ServiceContext& serviceContext) {
    LOGGER.info("Stopping Reticulum service");
    setRuntimeState(RuntimeState::Stopping);

    interfaceManager->stopAll();
    transportCore->clear();
    linkManager->clear();
    resourceManager->clear();

    if (dispatcher->isStarted()) {
        dispatcher->stop();
    }

    {
        auto lock = mutex.asScopedLock();
        lock.lock();
        observedAnnounces.clear();
        seenPathRequestKeys.clear();
    }

    setRuntimeState(RuntimeState::Stopped);
}

bool ReticulumService::registerInterface(const std::shared_ptr<Interface>& interfaceInstance) {
    const auto added = interfaceManager->addInterface(interfaceInstance, [this](InboundFrame frame) {
        onInboundFrame(std::move(frame));
    });

    if (!added || interfaceInstance == nullptr) {
        return false;
    }

    for (const auto& descriptor : interfaceManager->getInterfaces()) {
        if (descriptor.id == interfaceInstance->getId()) {
            publishEvent(ReticulumEvent {
                .type = EventType::InterfaceAttached,
                .runtimeState = getRuntimeState(),
                .interface = descriptor,
                .detail = std::format("Attached {} interface", descriptor.id)
            });

            publishEvent(ReticulumEvent {
                .type = descriptor.started ? EventType::InterfaceStarted : EventType::InterfaceStopped,
                .runtimeState = getRuntimeState(),
                .interface = descriptor,
                .detail = descriptor.started ? "Interface started" : "Interface registered but inactive"
            });
            return true;
        }
    }

    return true;
}

bool ReticulumService::unregisterInterface(const std::string& interfaceId) {
    const auto removed = interfaceManager->removeInterface(interfaceId);
    if (removed) {
        publishEvent(ReticulumEvent {
            .type = EventType::InterfaceDetached,
            .runtimeState = getRuntimeState(),
            .detail = std::format("Detached {} interface", interfaceId)
        });
    }
    return removed;
}

std::vector<InterfaceDescriptor> ReticulumService::getInterfaces() {
    return interfaceManager->getInterfaces();
}

bool ReticulumService::registerLocalDestination(const LocalDestination& destination) {
    if (!identityStore->isInitialized()) {
        return false;
    }

    const auto result = destinationRegistry->registerLocalDestination(destination, identityStore->getLocalIdentity());
    if (!result) {
        return false;
    }

    const auto registered = destinationRegistry->getLocalDestinations();
    const auto iterator = std::find_if(registered.begin(), registered.end(), [&destination](const auto& item) {
        return item.name == destination.name;
    });
    if (iterator == registered.end()) {
        return false;
    }

    publishEvent(ReticulumEvent {
        .type = EventType::LocalDestinationRegistered,
        .runtimeState = getRuntimeState(),
        .destination = iterator->hash,
        .detail = std::format("Registered local destination {} ({})", iterator->name, toHex(iterator->hash))
    });

    if (iterator->announceEnabled) {
        announceDestination(*iterator, false);
    }

    return true;
}

std::vector<RegisteredDestination> ReticulumService::getLocalDestinations() {
    return destinationRegistry->getLocalDestinations();
}

bool ReticulumService::announceLocalDestination(const DestinationHash& destinationHash) {
    if (const auto destination = destinationRegistry->findLocalDestination(destinationHash);
        destination.has_value() && destination->announceEnabled) {
        return announceDestination(*destination, false);
    }

    return false;
}

bool ReticulumService::requestPath(const DestinationHash& destinationHash, const std::vector<uint8_t>& tag) {
    if (destinationHash.empty()) {
        return false;
    }

    const auto packet = packetCodec->encodePathRequest(destinationHash, buildDiscoveryTag(tag));
    if (packet.empty()) {
        return false;
    }

    return broadcastPacket(packet);
}

bool ReticulumService::openLink(const DestinationHash& destinationHash, DestinationHash& outLinkId) {
    if (destinationHash.empty()) {
        return false;
    }

    const auto knownDestination = identityStore->getKnownDestination(destinationHash);
    const auto path = transportCore->getPath(destinationHash);
    if (!knownDestination.has_value() || !path.has_value() || path->interfaceId.empty() || path->unresponsive) {
        requestPath(destinationHash);
        return false;
    }

    std::vector<uint8_t> packet;
    if (!linkManager->beginInitiatorLink(
            destinationHash,
            knownDestination->identityHash,
            knownDestination->identityPublicKey,
            path->interfaceId,
            *packetCodec,
            outLinkId,
            packet)) {
        return false;
    }

    if (!sendPacketOnInterface(path->interfaceId, packet)) {
        linkManager->removeLink(outLinkId);
        return false;
    }

    if (const auto link = linkManager->getLink(outLinkId); link.has_value()) {
        publishLinkTableChanged(*link, std::format("Opened link request {}", toHex(outLinkId)));
    }

    return true;
}

bool ReticulumService::sendLinkData(const DestinationHash& linkId, uint8_t context, const std::vector<uint8_t>& plaintext) {
    const auto link = linkManager->getLink(linkId);
    if (!link.has_value() || link->interfaceId.empty() || link->state == LinkState::Closed) {
        return false;
    }

    std::vector<uint8_t> packet;
    if (!linkManager->encodeLinkData(linkId, context, plaintext, *packetCodec, packet)) {
        return false;
    }

    if (!sendPacketOnInterface(link->interfaceId, packet)) {
        return false;
    }

    if (const auto updatedLink = linkManager->getLink(linkId); updatedLink.has_value()) {
        publishLinkTableChanged(*updatedLink, std::format(
            "Sent link packet {} on {}",
            packetContextToString(context),
            toHex(linkId)
        ));
    }

    return true;
}

bool ReticulumService::identifyLink(const DestinationHash& linkId) {
    const auto localIdentity = identityStore->getLocalIdentity();

    std::vector<uint8_t> transcript;
    transcript.reserve(linkId.bytes.size() + localIdentity.publicKey.size());
    transcript.insert(transcript.end(), linkId.bytes.begin(), linkId.bytes.end());
    transcript.insert(transcript.end(), localIdentity.publicKey.begin(), localIdentity.publicKey.end());

    SignatureBytes signature {};
    if (!crypto::ed25519Sign(localIdentity.signingPrivateKey, transcript.data(), transcript.size(), signature)) {
        return false;
    }

    std::vector<uint8_t> plaintext;
    plaintext.reserve(localIdentity.publicKey.size() + signature.size());
    plaintext.insert(plaintext.end(), localIdentity.publicKey.begin(), localIdentity.publicKey.end());
    plaintext.insert(plaintext.end(), signature.begin(), signature.end());

    return sendLinkData(linkId, static_cast<uint8_t>(PacketContext::LinkIdentify), plaintext);
}

bool ReticulumService::closeLink(const DestinationHash& linkId) {
    const auto link = linkManager->getLink(linkId);
    if (!link.has_value()) {
        return false;
    }

    std::vector<uint8_t> plaintext(linkId.bytes.begin(), linkId.bytes.end());
    if (link->state != LinkState::Closed) {
        std::vector<uint8_t> packet;
        if (linkManager->encodeLinkData(linkId, static_cast<uint8_t>(PacketContext::LinkClose), plaintext, *packetCodec, packet)
            && !link->interfaceId.empty()) {
            sendPacketOnInterface(link->interfaceId, packet);
        }
    }

    const auto closed = linkManager->closeLink(linkId);
    if (closed) {
        if (const auto updatedLink = linkManager->getLink(linkId); updatedLink.has_value()) {
            publishLinkTableChanged(*updatedLink, std::format("Closed link {}", toHex(linkId)));
        }
    }

    return closed;
}

std::vector<AnnounceInfo> ReticulumService::getAnnounces() {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return observedAnnounces;
}

std::vector<PathEntry> ReticulumService::getPaths() {
    return transportCore->getPaths();
}

std::vector<LinkInfo> ReticulumService::getLinks() {
    return linkManager->getLinks();
}

std::vector<ResourceInfo> ReticulumService::getResources() {
    return resourceManager->getResources();
}

bool ReticulumService::sendFrame(const std::string& interfaceId, const InterfaceFrame& frame) {
    return interfaceManager->sendFrame(interfaceId, frame);
}

RuntimeState ReticulumService::getRuntimeState() {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return runtimeState;
}

void ReticulumService::onInboundFrame(InboundFrame frame) {
    if (!dispatcher->isStarted()) {
        return;
    }

    dispatcher->dispatch([this, frame = std::move(frame)]() mutable {
        publishEvent(ReticulumEvent {
            .type = EventType::InboundFrameQueued,
            .runtimeState = getRuntimeState(),
            .interface = toDescriptor(frame),
            .detail = std::format("Queued {} inbound bytes", frame.payload.size())
        });

        const auto decoded = packetCodec->decode(frame.payload);
        if (!decoded.has_value()) {
            publishEvent(ReticulumEvent {
                .type = EventType::Error,
                .runtimeState = getRuntimeState(),
                .interface = toDescriptor(frame),
                .detail = "Failed to decode inbound Reticulum packet"
            });
            return;
        }

        publishEvent(ReticulumEvent {
            .type = EventType::PacketDecoded,
            .runtimeState = getRuntimeState(),
            .interface = toDescriptor(frame),
            .packet = decoded->summary,
            .detail = std::format(
                "Decoded {} {} packet with {} context",
                destinationTypeToString(decoded->summary.destinationType),
                packetTypeToString(decoded->summary.packetType),
                packetContextToString(decoded->summary.context.value_or(static_cast<uint8_t>(PacketContext::None)))
            )
        });

        const auto pinnedDestination = identityStore->getKnownDestination(decoded->header.destination);
        const auto* pinnedIdentity = pinnedDestination.has_value() ? &pinnedDestination->identityPublicKey : nullptr;
        if (const auto announce = packetCodec->decodeAnnounce(frame, *decoded, pinnedIdentity); announce.has_value()) {
            observeAnnounce(*announce);

            identityStore->rememberDestination(IdentityStore::KnownDestination {
                .destinationHash = announce->destination,
                .identityHash = announce->identityHash,
                .nameHash = announce->nameHash,
                .identityPublicKey = announce->identityPublicKey,
                .appData = announce->appData,
                .latestRatchetPublicKey = announce->ratchetPublicKey,
                .lastAnnouncePacketHash = announce->packetHash,
                .lastAnnounceRandom = announce->announceRandom,
                .lastSeenTick = announce->observedTick
            });

            if (announce->ratchetPublicKey.has_value()) {
                identityStore->updateRatchet(announce->destination, *announce->ratchetPublicKey);
            }

            const PathEntry entry {
                .destination = announce->destination,
                .identityHash = announce->identityHash,
                .interfaceId = announce->interfaceId,
                .nextHop = announce->nextHop,
                .hops = announce->hops,
                .expiryTick = kernel::getTicks() + kernel::secondsToTicks(PATH_EXPIRY_SECONDS),
                .observedTick = announce->observedTick,
                .announceRandom = announce->announceRandom,
                .packetHash = announce->packetHash,
                .unresponsive = false
            };

            const auto existing = transportCore->getPath(entry.destination);
            if (shouldInstallPath(existing, entry)) {
                transportCore->installPath(entry);
                publishPathTableChanged(entry, existing.has_value()
                    ? std::format("Refreshed path for {} via {}", toHex(entry.destination), entry.interfaceId)
                    : std::format("Installed path for {} via {}", toHex(entry.destination), entry.interfaceId)
                );
            }
            return;
        }

        if (const auto pathRequest = packetCodec->decodePathRequest(frame, *decoded); pathRequest.has_value()) {
            bool seenBefore = false;
            {
                auto lock = mutex.asScopedLock();
                lock.lock();
                seenBefore = std::ranges::find(seenPathRequestKeys, pathRequest->duplicateKey) != seenPathRequestKeys.end();
                if (!seenBefore) {
                    seenPathRequestKeys.push_back(pathRequest->duplicateKey);
                    if (seenPathRequestKeys.size() > MAX_DISCOVERY_TAGS) {
                        seenPathRequestKeys.erase(seenPathRequestKeys.begin());
                    }
                }
            }

            if (seenBefore) {
                return;
            }

            if (const auto localDestination = destinationRegistry->findLocalDestination(pathRequest->requestedDestination);
                localDestination.has_value() && localDestination->announceEnabled) {
                announceDestination(*localDestination, true, frame.interfaceId);
            }
            return;
        }

        DestinationHash linkId {};
        if (const auto linkRequest = packetCodec->decodeLinkRequest(frame, *decoded, linkId); linkRequest.has_value()) {
            if (const auto localDestination = destinationRegistry->findLocalDestination(linkRequest->destination);
                localDestination.has_value() && localDestination->acceptsLinks && localDestination->type == DestinationType::Single) {
                if (const auto response = linkManager->acceptLinkRequest(*linkRequest, *localDestination, identityStore->getLocalIdentity(), *packetCodec);
                    response.has_value()) {
                    sendPacketOnInterface(linkRequest->interfaceId, *response);
                    if (const auto link = linkManager->getLink(linkRequest->linkId); link.has_value()) {
                        publishLinkTableChanged(*link, std::format("Accepted link request {}", toHex(linkRequest->linkId)));
                    }
                }
            }
            return;
        }

        if (const auto linkProof = packetCodec->decodeLinkProof(frame, *decoded); linkProof.has_value()) {
            if (const auto response = linkManager->acceptLinkProof(*linkProof, *packetCodec); response.has_value()) {
                sendPacketOnInterface(linkProof->interfaceId, *response);
                if (const auto link = linkManager->getLink(linkProof->linkId); link.has_value()) {
                    publishLinkTableChanged(*link, std::format("Activated link {}", toHex(linkProof->linkId)));
                }
            }
            return;
        }

        if (const auto linkData = linkManager->decodeLinkData(*decoded); linkData.has_value()) {
            if (linkData->context == static_cast<uint8_t>(PacketContext::Keepalive) &&
                linkData->plaintext.size() == 1 &&
                linkData->plaintext[0] == KEEPALIVE_REQUEST) {
                std::vector<uint8_t> replyPacket;
                if (linkManager->encodeLinkData(linkData->linkId, static_cast<uint8_t>(PacketContext::Keepalive), { KEEPALIVE_REPLY }, *packetCodec, replyPacket)) {
                    sendPacketOnInterface(linkData->link.interfaceId, replyPacket);
                }
            } else if (linkData->context == static_cast<uint8_t>(PacketContext::LinkClose)) {
                linkManager->closeLink(linkData->linkId);
            }

            publishLinkTableChanged(linkData->link, std::format(
                "Processed link packet {} on {}",
                packetContextToString(linkData->context),
                toHex(linkData->linkId)
            ));
        }
    });
}

std::shared_ptr<ReticulumService> findService() {
    return service::findServiceById<ReticulumService>(manifest.id);
}

extern const ServiceManifest manifest = {
    .id = "Reticulum",
    .createService = create<ReticulumService>
};

} // namespace tt::service::reticulum
