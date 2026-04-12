#include <Tactility/hal/radio/LoRaDevice.h>

#include <algorithm>

namespace tt::hal::radio {

std::vector<std::shared_ptr<LoRaDevice>> findLoRaDevices() {
    std::vector<std::shared_ptr<LoRaDevice>> radios;
    for (const auto& device : tt::hal::findDevices(tt::hal::Device::Type::Radio)) {
        radios.push_back(std::static_pointer_cast<LoRaDevice>(device));
    }
    return radios;
}

std::shared_ptr<LoRaDevice> findLoRaDevice(std::string_view preferredName) {
    const auto radios = findLoRaDevices();
    if (radios.empty()) {
        return nullptr;
    }

    if (!preferredName.empty()) {
        const auto iterator = std::find_if(radios.begin(), radios.end(), [preferredName](const auto& radio) {
            return radio != nullptr && radio->getName() == preferredName;
        });
        if (iterator != radios.end()) {
            return *iterator;
        }
    }

    return radios.front();
}

} // namespace tt::hal::radio
