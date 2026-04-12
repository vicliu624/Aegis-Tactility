#include <Tactility/service/reticulum/interfaces/backends/NativeLoRaBackend.h>

namespace tt::service::reticulum::interfaces::backends {

InterfaceMetrics NativeLoRaBackend::toInterfaceMetrics(const tt::hal::radio::LoRaMetrics& metrics) {
    return InterfaceMetrics {
        .available = metrics.available,
        .bitrate = metrics.bitrate,
        .rssi = metrics.rssi,
        .snr = metrics.snr,
        .hardwareMtu = metrics.hardwareMtu
    };
}

std::string NativeLoRaBackend::getName() const {
    return device != nullptr ? device->getName() : "Native LoRa backend";
}

std::string NativeLoRaBackend::getDetail() const {
    return device != nullptr ? device->getDetail() : "No native LoRa device is available";
}

bool NativeLoRaBackend::start(const settings::reticulum::LoRaSettings& settings, ReceiveCallback receiveCallback) {
    if (device == nullptr) {
        return false;
    }

    const tt::hal::radio::LoRaConfiguration configuration {
        .frequency = settings.frequency,
        .bandwidth = settings.bandwidth,
        .txPower = settings.txPower,
        .spreadingFactor = settings.spreadingFactor,
        .codingRate = settings.codingRate,
        .preambleLength = 8U,
        .syncWord = 0x12U,
        .crc = true
    };

    return device->start(configuration, std::move(receiveCallback));
}

void NativeLoRaBackend::stop() {
    if (device != nullptr) {
        device->stop();
    }
}

bool NativeLoRaBackend::poll(TickType_t timeout) {
    return device != nullptr && device->poll(timeout);
}

bool NativeLoRaBackend::send(const std::vector<uint8_t>& payload) {
    return device != nullptr && device->send(payload);
}

InterfaceMetrics NativeLoRaBackend::getMetrics() const {
    return device != nullptr ? toInterfaceMetrics(device->getMetrics()) : InterfaceMetrics {};
}

} // namespace tt::service::reticulum::interfaces::backends
