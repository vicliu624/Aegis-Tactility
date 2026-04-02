#include <Tactility/Logger.h>
#include <Tactility/kernel/Kernel.h>
#include <Tactility/service/reticulum/interfaces/LoRaInterface.h>

#include <tactility/device.h>
#include <tactility/drivers/uart_controller.h>
#include <tactility/error.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace tt::service::reticulum::interfaces {

static const auto LOGGER = Logger("RNS-LoRa");
static constexpr uint32_t REQUIRED_FW_MAJ = 1;
static constexpr uint32_t REQUIRED_FW_MIN = 52;
static constexpr int16_t RSSI_OFFSET = 157;

std::vector<uint8_t> LoRaInterface::escape(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> escaped;
    escaped.reserve(data.size());
    for (const auto byte : data) {
        if (byte == Kiss::Fesc) {
            escaped.push_back(Kiss::Fesc);
            escaped.push_back(Kiss::Tfesc);
        } else if (byte == Kiss::Fend) {
            escaped.push_back(Kiss::Fesc);
            escaped.push_back(Kiss::Tfend);
        } else {
            escaped.push_back(byte);
        }
    }
    return escaped;
}

InterfaceMetrics LoRaInterface::getMetrics() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return metrics;
}

// The current LoRa bearer adapter speaks to an attached RNode modem, but
// Reticulum packet semantics stay above this host-transport boundary.
bool LoRaInterface::resolveBackendDevice() {
    backendDeviceName.clear();

    if (auto* preferred = device_find_by_name(PreferredBackendDevice); preferred != nullptr) {
        backendDeviceName = preferred->name;
        uart = preferred;
        return true;
    }

    device_for_each_of_type(&UART_CONTROLLER_TYPE, this, [](auto* device, auto* context) {
        auto* self = static_cast<LoRaInterface*>(context);
        self->backendDeviceName = device->name;
        self->uart = device;
        return false;
    });

    return uart != nullptr;
}

bool LoRaInterface::openUart() {
    uart = nullptr;
    if (!resolveBackendDevice()) {
        LOGGER.error("Failed to find a UART backend for the LoRa RNode adapter");
        return false;
    }

    const UartConfig config {
        .baud_rate = BackendBaudRate,
        .data_bits = UART_CONTROLLER_DATA_8_BITS,
        .parity = UART_CONTROLLER_PARITY_DISABLE,
        .stop_bits = UART_CONTROLLER_STOP_BITS_1
    };

    if (const auto error = uart_controller_set_config(uart, &config); error != ERROR_NONE) {
        LOGGER.error("Failed to configure backend UART {}: {}", backendDeviceName, error_to_string(error));
        uart = nullptr;
        backendDeviceName.clear();
        return false;
    }

    if (const auto error = uart_controller_open(uart); error != ERROR_NONE) {
        LOGGER.error("Failed to open backend UART {}: {}", backendDeviceName, error_to_string(error));
        uart = nullptr;
        backendDeviceName.clear();
        return false;
    }

    uart_controller_flush_input(uart);
    return true;
}

void LoRaInterface::closeUart() {
    if (uart != nullptr) {
        if (uart_controller_is_open(uart)) {
            const auto error = uart_controller_close(uart);
            if (error != ERROR_NONE) {
                LOGGER.warn("Failed to close backend UART {}: {}", backendDeviceName, error_to_string(error));
            }
        }
        uart = nullptr;
    }
    backendDeviceName.clear();
}

bool LoRaInterface::writeFrameBytes(const std::vector<uint8_t>& bytes) {
    if (uart == nullptr || bytes.empty()) {
        return false;
    }

    const auto error = uart_controller_write_bytes(uart, bytes.data(), bytes.size(), 500 / portTICK_PERIOD_MS);
    if (error != ERROR_NONE) {
        LOGGER.warn("UART write failed on backend {}: {}", backendDeviceName, error_to_string(error));
        return false;
    }

    return true;
}

bool LoRaInterface::writeKissCommand(uint8_t command, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> frame;
    const auto escapedData = escape(data);
    frame.reserve(escapedData.size() + 3);
    frame.push_back(Kiss::Fend);
    frame.push_back(command);
    frame.insert(frame.end(), escapedData.begin(), escapedData.end());
    frame.push_back(Kiss::Fend);
    return writeFrameBytes(frame);
}

void LoRaInterface::updateBitrateLocked() {
    if (reportedBandwidth == 0 || reportedSf == 0 || reportedCr == 0) {
        metrics.bitrate = 0;
        return;
    }

    const auto denominator = std::pow(2.0, static_cast<double>(reportedSf)) / (static_cast<double>(reportedBandwidth) / 1000.0);
    if (denominator <= 0.0) {
        metrics.bitrate = 0;
        return;
    }

    const auto bitrate = static_cast<int32_t>(std::round(reportedSf * ((4.0 / reportedCr) / denominator) * 1000.0));
    metrics.bitrate = std::max<int32_t>(bitrate, 0);
}

bool LoRaInterface::validateConfiguredRadio() const {
    auto lock = mutex.asScopedLock();
    lock.lock();

    if (!detected || !firmwareOk) {
        return false;
    }

    return reportedFrequency != 0 &&
        std::llabs(static_cast<long long>(settings.frequency) - static_cast<long long>(reportedFrequency)) <= 100LL &&
        settings.bandwidth == reportedBandwidth &&
        settings.txPower == reportedTxPower &&
        settings.spreadingFactor == reportedSf &&
        settings.codingRate == reportedCr &&
        reportedRadioState == Kiss::RadioStateOn;
}

void LoRaInterface::processReceivedPacket(const std::vector<uint8_t>& payload) {
    if (!receiveCallback || payload.empty()) {
        return;
    }

    InboundFrame frame;
    frame.interfaceId = getId();
    frame.interfaceKind = getKind();
    frame.metrics = getMetrics();
    frame.payload = payload;
    receiveCallback(std::move(frame));
}

void LoRaInterface::processQueue() {
    std::vector<uint8_t> queued;
    {
        auto lock = mutex.asScopedLock();
        lock.lock();
        if (packetQueue.empty()) {
            interfaceReady = true;
            return;
        }

        queued = std::move(packetQueue.front());
        packetQueue.erase(packetQueue.begin());
        interfaceReady = true;
    }

    InterfaceFrame frame;
    frame.payload = std::move(queued);
    sendFrame(frame);
}

bool LoRaInterface::configureDevice() {
    kernel::delayMillis(2000);

    {
        auto lock = mutex.asScopedLock();
        lock.lock();
        detected = false;
        firmwareOk = false;
        firmwareMajor = 0;
        firmwareMinor = 0;
        reportedFrequency = 0;
        reportedBandwidth = 0;
        reportedTxPower = 0;
        reportedSf = 0;
        reportedCr = 0;
        reportedRadioState = Kiss::RadioStateOff;
        metrics = InterfaceMetrics {
            .available = false,
            .bitrate = 0,
            .rssi = 0,
            .snr = 0,
            .hardwareMtu = HardwareMtu
        };
    }

    if (!writeFrameBytes({
            Kiss::Fend, Kiss::CmdDetect, Kiss::DetectRequest, Kiss::Fend,
            Kiss::Fend, Kiss::CmdFwVersion, 0x00, Kiss::Fend,
            Kiss::Fend, Kiss::CmdPlatform, 0x00, Kiss::Fend,
            Kiss::Fend, Kiss::CmdMcu, 0x00, Kiss::Fend
        })) {
        return false;
    }

    const auto detectDeadline = kernel::getTicks() + kernel::secondsToTicks(2);
    while (kernel::getTicks() < detectDeadline) {
        {
            auto lock = mutex.asScopedLock();
            lock.lock();
            if (detected && firmwareOk) {
                break;
            }
        }
        kernel::delayMillis(50);
    }

    {
        auto lock = mutex.asScopedLock();
        lock.lock();
        if (!detected) {
            LOGGER.error("Could not detect RNode on backend {}", backendDeviceName);
            return false;
        }
        if (!firmwareOk) {
            LOGGER.error("RNode firmware on backend {} is too old: {}.{}", backendDeviceName, firmwareMajor, firmwareMinor);
            return false;
        }
    }

    auto encodeU32 = [](uint32_t value) {
        return std::vector<uint8_t> {
            static_cast<uint8_t>(value >> 24),
            static_cast<uint8_t>((value >> 16) & 0xFF),
            static_cast<uint8_t>((value >> 8) & 0xFF),
            static_cast<uint8_t>(value & 0xFF)
        };
    };

    if (!writeKissCommand(Kiss::CmdFrequency, encodeU32(settings.frequency)) ||
        !writeKissCommand(Kiss::CmdBandwidth, encodeU32(settings.bandwidth)) ||
        !writeKissCommand(Kiss::CmdTxPower, { settings.txPower }) ||
        !writeKissCommand(Kiss::CmdSf, { settings.spreadingFactor }) ||
        !writeKissCommand(Kiss::CmdCr, { settings.codingRate }) ||
        !writeKissCommand(Kiss::CmdRadioState, { Kiss::RadioStateOn })) {
        return false;
    }

    const auto validationDeadline = kernel::getTicks() + kernel::secondsToTicks(2);
    while (kernel::getTicks() < validationDeadline) {
        if (validateConfiguredRadio()) {
            break;
        }
        kernel::delayMillis(50);
    }

    if (!validateConfiguredRadio()) {
        LOGGER.error("RNode on backend {} did not report the configured LoRa parameters back", backendDeviceName);
        return false;
    }

    auto lock = mutex.asScopedLock();
    lock.lock();
    online = true;
    interfaceReady = true;
    metrics.available = true;
    metrics.hardwareMtu = HardwareMtu;
    return true;
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

    auto lock = mutex.asScopedLock();
    lock.lock();
    if (thread != nullptr && thread->getState() != Thread::State::Stopped) {
        return true;
    }

    threadInterrupted = false;
    metrics = InterfaceMetrics {
        .available = false,
        .bitrate = 0,
        .rssi = 0,
        .snr = 0,
        .hardwareMtu = HardwareMtu
    };
    thread = std::make_unique<Thread>("reticulum_lora", 6144, [this] {
        return readLoop();
    });
    thread->setPriority(Thread::Priority::High);
    thread->start();
    return true;
}

void LoRaInterface::stop() {
    std::unique_ptr<Thread> ownedThread;

    {
        auto lock = mutex.asScopedLock();
        lock.lock();
        threadInterrupted = true;
        online = false;
        interfaceReady = false;
        metrics.available = false;
        ownedThread = std::move(thread);
    }

    if (uart != nullptr) {
        writeKissCommand(Kiss::CmdRadioState, { Kiss::RadioStateOff });
        writeKissCommand(Kiss::CmdLeave, { 0xFF });
    }

    if (ownedThread != nullptr && ownedThread->getState() != Thread::State::Stopped) {
        ownedThread->join(kernel::secondsToTicks(2));
    }

    closeUart();
}

int32_t LoRaInterface::readLoop() {
    if (!openUart()) {
        return -1;
    }

    if (!configureDevice()) {
        closeUart();
        return -1;
    }

    bool inFrame = false;
    bool escapeNext = false;
    uint8_t command = Kiss::CmdUnknown;
    std::vector<uint8_t> dataBuffer;
    std::vector<uint8_t> commandBuffer;

    while (true) {
        {
            auto lock = mutex.asScopedLock();
            lock.lock();
            if (threadInterrupted) {
                break;
            }
        }

        size_t available = 0;
        if (uart_controller_get_available(uart, &available) != ERROR_NONE || available == 0) {
            kernel::delayMillis(50);
            continue;
        }

        uint8_t byte = 0;
        if (uart_controller_read_byte(uart, &byte, 20 / portTICK_PERIOD_MS) != ERROR_NONE) {
            continue;
        }

        if (inFrame && byte == Kiss::Fend && command == Kiss::CmdData) {
            inFrame = false;
            processReceivedPacket(dataBuffer);
            dataBuffer.clear();
            commandBuffer.clear();
            continue;
        }

        if (byte == Kiss::Fend) {
            inFrame = true;
            escapeNext = false;
            command = Kiss::CmdUnknown;
            dataBuffer.clear();
            commandBuffer.clear();
            continue;
        }

        if (!inFrame || dataBuffer.size() >= HardwareMtu) {
            continue;
        }

        if (command == Kiss::CmdUnknown && dataBuffer.empty()) {
            command = byte;
            continue;
        }

        auto pushDecodedByte = [&escapeNext](std::vector<uint8_t>& buffer, uint8_t rawByte) {
            auto decoded = rawByte;
            if (rawByte == Kiss::Fesc) {
                escapeNext = true;
                return false;
            }

            if (escapeNext) {
                if (rawByte == Kiss::Tfend) {
                    decoded = Kiss::Fend;
                } else if (rawByte == Kiss::Tfesc) {
                    decoded = Kiss::Fesc;
                }
                escapeNext = false;
            }

            buffer.push_back(decoded);
            return true;
        };

        if (command == Kiss::CmdData) {
            pushDecodedByte(dataBuffer, byte);
            continue;
        }

        if (!pushDecodedByte(commandBuffer, byte)) {
            continue;
        }

        auto lock = mutex.asScopedLock();
        lock.lock();

        switch (command) {
            case Kiss::CmdFrequency:
                if (commandBuffer.size() == 4) {
                    reportedFrequency = (static_cast<uint32_t>(commandBuffer[0]) << 24) |
                        (static_cast<uint32_t>(commandBuffer[1]) << 16) |
                        (static_cast<uint32_t>(commandBuffer[2]) << 8) |
                        static_cast<uint32_t>(commandBuffer[3]);
                    updateBitrateLocked();
                }
                break;
            case Kiss::CmdBandwidth:
                if (commandBuffer.size() == 4) {
                    reportedBandwidth = (static_cast<uint32_t>(commandBuffer[0]) << 24) |
                        (static_cast<uint32_t>(commandBuffer[1]) << 16) |
                        (static_cast<uint32_t>(commandBuffer[2]) << 8) |
                        static_cast<uint32_t>(commandBuffer[3]);
                    updateBitrateLocked();
                }
                break;
            case Kiss::CmdTxPower:
                reportedTxPower = commandBuffer.back();
                break;
            case Kiss::CmdSf:
                reportedSf = commandBuffer.back();
                updateBitrateLocked();
                break;
            case Kiss::CmdCr:
                reportedCr = commandBuffer.back();
                updateBitrateLocked();
                break;
            case Kiss::CmdRadioState:
                reportedRadioState = commandBuffer.back();
                break;
            case Kiss::CmdFwVersion:
                if (commandBuffer.size() == 2) {
                    firmwareMajor = commandBuffer[0];
                    firmwareMinor = commandBuffer[1];
                    firmwareOk = firmwareMajor > REQUIRED_FW_MAJ ||
                        (firmwareMajor == REQUIRED_FW_MAJ && firmwareMinor >= REQUIRED_FW_MIN);
                }
                break;
            case Kiss::CmdDetect:
                if (!commandBuffer.empty()) {
                    detected = commandBuffer.back() == Kiss::DetectResponse;
                }
                break;
            case Kiss::CmdStatRssi:
                if (!commandBuffer.empty()) {
                    metrics.rssi = static_cast<int16_t>(static_cast<int16_t>(commandBuffer.back()) - RSSI_OFFSET);
                }
                break;
            case Kiss::CmdStatSnr:
                if (!commandBuffer.empty()) {
                    metrics.snr = static_cast<int16_t>(std::lround(static_cast<double>(static_cast<int8_t>(commandBuffer.back())) * 0.25));
                }
                break;
            case Kiss::CmdReady:
                lock.unlock();
                processQueue();
                break;
            case Kiss::CmdError:
                if (!commandBuffer.empty()) {
                    LOGGER.warn("RNode on backend {} reported hardware error code 0x{:02X}", backendDeviceName, commandBuffer.back());
                }
                break;
            default:
                break;
        }
    }

    closeUart();
    return 0;
}

bool LoRaInterface::sendFrame(const InterfaceFrame& frame) {
    if (frame.payload.empty()) {
        return false;
    }

    {
        auto lock = mutex.asScopedLock();
        lock.lock();
        if (!online) {
            LOGGER.warn("LoRa send requested while interface is offline");
            return false;
        }

        if (settings.flowControl && !interfaceReady) {
            packetQueue.push_back(frame.payload);
            return true;
        }

        if (settings.flowControl) {
            interfaceReady = false;
        }
    }

    const auto escapedPayload = escape(frame.payload);
    std::vector<uint8_t> kissFrame;
    kissFrame.reserve(escapedPayload.size() + 3);
    kissFrame.push_back(Kiss::Fend);
    kissFrame.push_back(Kiss::CmdData);
    kissFrame.insert(kissFrame.end(), escapedPayload.begin(), escapedPayload.end());
    kissFrame.push_back(Kiss::Fend);
    return writeFrameBytes(kissFrame);
}

} // namespace tt::service::reticulum::interfaces
