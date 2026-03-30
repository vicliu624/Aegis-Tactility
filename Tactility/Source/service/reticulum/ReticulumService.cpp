#include <Tactility/service/reticulum/Reticulum.h>

#include <Tactility/Logger.h>
#include <Tactility/file/File.h>
#include <Tactility/service/ServiceManifest.h>
#include <Tactility/service/ServiceRegistration.h>
#include <Tactility/service/reticulum/DestinationRegistry.h>
#include <Tactility/service/reticulum/IdentityStore.h>
#include <Tactility/service/reticulum/InterfaceManager.h>
#include <Tactility/service/reticulum/LinkManager.h>
#include <Tactility/service/reticulum/PacketCodec.h>
#include <Tactility/service/reticulum/ResourceManager.h>
#include <Tactility/service/reticulum/ReticulumService.h>
#include <Tactility/service/reticulum/TransportCore.h>
#include <Tactility/service/reticulum/interfaces/EspNowInterface.h>

#include <algorithm>
#include <format>

namespace tt::service::reticulum {

static const auto LOGGER = Logger("Reticulum");
extern const ServiceManifest manifest;

static InterfaceDescriptor toDescriptor(const InboundFrame& frame) {
    return InterfaceDescriptor {
        .id = frame.interfaceId,
        .kind = frame.interfaceKind,
        .capabilities = toMask(InterfaceCapability::None),
        .started = true,
        .metrics = frame.metrics
    };
}

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

bool ReticulumService::onStart(ServiceContext& serviceContext) {
    LOGGER.info("Starting Reticulum service milestone shell");

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

    setRuntimeState(RuntimeState::Stopped);
}

bool ReticulumService::registerInterface(const std::shared_ptr<Interface>& interfaceInstance) {
    const auto added = interfaceManager->addInterface(interfaceInstance, [this](InboundFrame frame) {
        onInboundFrame(std::move(frame));
    });

    if (!added || interfaceInstance == nullptr) {
        return false;
    }

    const auto descriptors = interfaceManager->getInterfaces();
    for (const auto& descriptor : descriptors) {
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

    const auto result = destinationRegistry->registerLocalDestination(destination, identityStore->getBootstrapIdentity());
    if (!result) {
        return false;
    }

    const auto registered = destinationRegistry->getLocalDestinations();
    const auto iterator = std::find_if(registered.begin(), registered.end(), [&destination](const auto& item) {
        return item.name == destination.name;
    });
    if (iterator != registered.end()) {
        publishEvent(ReticulumEvent {
            .type = EventType::LocalDestinationRegistered,
            .runtimeState = getRuntimeState(),
            .destination = iterator->hash,
            .detail = std::format("Registered local destination {} ({})", iterator->name, toHex(iterator->hash))
        });
    }

    return true;
}

std::vector<RegisteredDestination> ReticulumService::getLocalDestinations() {
    return destinationRegistry->getLocalDestinations();
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

        const auto packet = packetCodec->summarize(frame.payload);
        if (!packet.has_value()) {
            publishEvent(ReticulumEvent {
                .type = EventType::Error,
                .runtimeState = getRuntimeState(),
                .interface = toDescriptor(frame),
                .detail = "Failed to summarize inbound packet"
            });
            return;
        }

        publishEvent(ReticulumEvent {
            .type = EventType::PacketDecoded,
            .runtimeState = getRuntimeState(),
            .interface = toDescriptor(frame),
            .packet = packet,
            .detail = "Captured packet envelope summary"
        });
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
