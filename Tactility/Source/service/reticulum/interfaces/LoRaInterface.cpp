#include <Tactility/service/reticulum/interfaces/LoRaInterface.h>

#include <Tactility/Logger.h>
#include <Tactility/hal/radio/LoRaDevice.h>
#include <Tactility/service/reticulum/interfaces/backends/NativeLoRaBackend.h>
#include <Tactility/service/reticulum/interfaces/backends/UartKissLoRaBackend.h>

namespace tt::service::reticulum::interfaces {

namespace {

static const auto LOGGER = Logger("RNS-LoRa");

std::shared_ptr<backends::LoRaBackend> createBackend() {
    if (auto nativeDevice = tt::hal::radio::findLoRaDevice(); nativeDevice != nullptr) {
        return std::make_shared<backends::NativeLoRaBackend>(std::move(nativeDevice));
    }

    return std::make_shared<backends::UartKissLoRaBackend>();
}

} // namespace

LoRaInterface::~LoRaInterface() = default;

InterfaceMetrics LoRaInterface::getMetrics() const {
    std::shared_ptr<backends::LoRaBackend> activeBackend;
    {
        auto lock = mutex.asScopedLock();
        lock.lock();
        activeBackend = backend;
    }
    return activeBackend != nullptr ? activeBackend->getMetrics() : InterfaceMetrics {};
}

void LoRaInterface::handleReceivedPayload(std::vector<uint8_t> payload) {
    ReceiveCallback callback;
    {
        auto lock = mutex.asScopedLock();
        lock.lock();
        callback = receiveCallback;
    }

    if (callback == nullptr || payload.empty()) {
        return;
    }

    InboundFrame frame;
    frame.interfaceId = getId();
    frame.interfaceKind = getKind();
    frame.metrics = getMetrics();
    frame.payload = std::move(payload);
    callback(std::move(frame));
}

int32_t LoRaInterface::runLoop() {
    while (true) {
        std::shared_ptr<backends::LoRaBackend> activeBackend;

        {
            auto lock = mutex.asScopedLock();
            lock.lock();
            if (threadInterrupted || backend == nullptr) {
                return 0;
            }
            activeBackend = backend;
        }

        if (!activeBackend->poll(pdMS_TO_TICKS(50))) {
            LOGGER.error("{} backend poll failed: {}", activeBackend->getName(), activeBackend->getDetail());
            activeBackend->stop();
            return -1;
        }
    }
}

bool LoRaInterface::start() {
    std::optional<std::string> validationError;
    if (!settings::reticulum::validate(settings, validationError)) {
        LOGGER.error("Invalid LoRa settings: {}", validationError.value_or("unknown error"));
        return false;
    }

    if (!settings.enabled) {
        LOGGER.info("LoRa interface disabled in settings");
        return false;
    }

    auto selectedBackend = createBackend();
    if (selectedBackend == nullptr) {
        LOGGER.error("No LoRa backend is available");
        return false;
    }

    LOGGER.info("Starting LoRa interface with settings: enabled=true, frequency={}Hz, bandwidth={}Hz, txPower={}dBm, sf={}, cr={}, flowControl={}",
        settings.frequency,
        settings.bandwidth,
        settings.txPower,
        settings.spreadingFactor,
        settings.codingRate,
        settings.flowControl ? "true" : "false"
    );
    LOGGER.info("Using {} backend {}", tt::hal::radio::findLoRaDevice() != nullptr ? "native LoRa device" : "UART KISS", selectedBackend->getName());

    if (!selectedBackend->start(settings, [this](std::vector<uint8_t> payload) {
            handleReceivedPayload(std::move(payload));
        })) {
        LOGGER.error("{} backend failed to start: {}", selectedBackend->getName(), selectedBackend->getDetail());
        return false;
    }

    LOGGER.info("{} started with {}", selectedBackend->getName(), selectedBackend->getDetail());

    auto worker = std::make_unique<Thread>("reticulum_lora", 6144, [this] {
        return runLoop();
    });
    worker->setPriority(Thread::Priority::High);

    {
        auto lock = mutex.asScopedLock();
        lock.lock();
        threadInterrupted = false;
        backend = std::move(selectedBackend);
        thread = std::move(worker);
        thread->start();
    }

    LOGGER.info("LoRa interface thread started");
    return true;
}

void LoRaInterface::stop() {
    std::unique_ptr<Thread> ownedThread;
    std::shared_ptr<backends::LoRaBackend> ownedBackend;

    {
        auto lock = mutex.asScopedLock();
        lock.lock();
        threadInterrupted = true;
        ownedThread = std::move(thread);
    }

    if (ownedThread != nullptr && ownedThread->getState() != Thread::State::Stopped) {
        ownedThread->join(kernel::secondsToTicks(2));
    }

    {
        auto lock = mutex.asScopedLock();
        lock.lock();
        ownedBackend = std::move(backend);
    }

    if (ownedBackend != nullptr) {
        ownedBackend->stop();
    }
}

bool LoRaInterface::sendFrame(const InterfaceFrame& frame) {
    if (frame.payload.empty()) {
        return false;
    }

    std::shared_ptr<backends::LoRaBackend> activeBackend;
    InterfaceMetrics metrics {};
    {
        auto lock = mutex.asScopedLock();
        lock.lock();
        activeBackend = backend;
        metrics = activeBackend != nullptr ? activeBackend->getMetrics() : InterfaceMetrics {};
    }

    if (activeBackend == nullptr || !metrics.available) {
        LOGGER.warn("LoRa send requested while interface is offline");
        return false;
    }

    if (metrics.hardwareMtu > 0 && frame.payload.size() > metrics.hardwareMtu) {
        LOGGER.warn("LoRa payload size {} exceeds hardware MTU {}", frame.payload.size(), metrics.hardwareMtu);
        return false;
    }

    return activeBackend->send(frame.payload);
}

} // namespace tt::service::reticulum::interfaces
