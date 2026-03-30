#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#include <Tactility/service/reticulum/interfaces/EspNowInterface.h>

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && !defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include <Tactility/service/espnow/EspNow.h>

namespace tt::service::reticulum::interfaces {

static const auto LOGGER = Logger("RNS-EspNow");
static constexpr uint8_t BROADCAST_MAC[ESP_NOW_ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

InterfaceMetrics EspNowInterface::getMetrics() const {
    InterfaceMetrics metrics;
    metrics.available = service::espnow::isEnabled();
    metrics.hardwareMtu = service::espnow::getMaxDataLength();
    return metrics;
}

bool EspNowInterface::start() {
    if (receiveSubscription != service::espnow::NO_SUBSCRIPTION) {
        return true;
    }

    receiveSubscription = service::espnow::subscribeReceiver(
        [this](const esp_now_recv_info_t* receiveInfo, const uint8_t* data, int length) {
            if (!receiveCallback || length <= 0) {
                return;
            }

            InboundFrame frame;
            frame.interfaceId = getId();
            frame.interfaceKind = getKind();
            frame.metrics = getMetrics();
            frame.nextHop.assign(receiveInfo->src_addr, receiveInfo->src_addr + ESP_NOW_ETH_ALEN);
            frame.payload.assign(data, data + length);
            receiveCallback(std::move(frame));
        }
    );

    if (receiveSubscription == service::espnow::NO_SUBSCRIPTION) {
        LOGGER.warn("Failed to subscribe to ESP-NOW frames");
        return false;
    }

    LOGGER.info("ESP-NOW interface attached");
    return true;
}

void EspNowInterface::stop() {
    if (receiveSubscription != service::espnow::NO_SUBSCRIPTION) {
        service::espnow::unsubscribeReceiver(receiveSubscription);
        receiveSubscription = service::espnow::NO_SUBSCRIPTION;
        LOGGER.info("ESP-NOW interface detached");
    }
}

bool EspNowInterface::sendFrame(const InterfaceFrame& frame) {
    if (frame.payload.empty()) {
        return false;
    }

    const uint8_t* target = BROADCAST_MAC;
    if (!frame.broadcast) {
        if (frame.nextHop.size() != ESP_NOW_ETH_ALEN) {
            LOGGER.warn("nextHop must be {} bytes for ESP-NOW", ESP_NOW_ETH_ALEN);
            return false;
        }
        target = frame.nextHop.data();
    }

    return service::espnow::send(target, frame.payload.data(), frame.payload.size());
}

} // namespace tt::service::reticulum::interfaces

#else

namespace tt::service::reticulum::interfaces {

InterfaceMetrics EspNowInterface::getMetrics() const {
    return InterfaceMetrics {};
}

bool EspNowInterface::start() {
    return false;
}

void EspNowInterface::stop() {}

bool EspNowInterface::sendFrame(const InterfaceFrame& frame) {
    return false;
}

} // namespace tt::service::reticulum::interfaces

#endif
