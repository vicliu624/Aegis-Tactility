#include <Tactility/Logger.h>
#include <Tactility/service/reticulum/interfaces/LoRaInterface.h>

namespace tt::service::reticulum::interfaces {

static const auto LOGGER = Logger("RNS-LoRa");

bool LoRaInterface::start() {
    LOGGER.warn("LoRa interface backend is not wired into this milestone yet");
    return false;
}

bool LoRaInterface::sendFrame(const InterfaceFrame& frame) {
    LOGGER.warn("LoRa send requested before a hardware backend exists ({} bytes)", frame.payload.size());
    return false;
}

} // namespace tt::service::reticulum::interfaces
