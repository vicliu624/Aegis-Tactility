#include "TdeckSx1262LoRaDevice.h"

#include <Tactility/Logger.h>

#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <tactility/device.h>
#include <tactility/drivers/esp32_spi.h>
#include <tactility/drivers/spi_controller.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <format>
#include <memory>
#include <utility>

namespace {

static const auto LOGGER = tt::Logger("TDeckSX1262");

constexpr gpio_num_t TdeckLoRaChipSelectPin = GPIO_NUM_9;
constexpr gpio_num_t TdeckLoRaResetPin = GPIO_NUM_17;
constexpr gpio_num_t TdeckLoRaBusyPin = GPIO_NUM_13;
constexpr gpio_num_t TdeckLoRaIrqPin = GPIO_NUM_45;

constexpr int SpiClockHz = 4'000'000;
constexpr size_t PacketBufferSize = 255;

constexpr uint8_t CmdSetStandby = 0x80;
constexpr uint8_t CmdSetRx = 0x82;
constexpr uint8_t CmdSetTx = 0x83;
constexpr uint8_t CmdSetRfFrequency = 0x86;
constexpr uint8_t CmdSetPacketType = 0x8A;
constexpr uint8_t CmdSetModulationParams = 0x8B;
constexpr uint8_t CmdSetPacketParams = 0x8C;
constexpr uint8_t CmdSetTxParams = 0x8E;
constexpr uint8_t CmdSetBufferBaseAddress = 0x8F;
constexpr uint8_t CmdSetRxTxFallbackMode = 0x93;
constexpr uint8_t CmdSetPaConfig = 0x95;
constexpr uint8_t CmdSetRegulatorMode = 0x96;
constexpr uint8_t CmdCalibrate = 0x89;
constexpr uint8_t CmdCalibrateImage = 0x98;
constexpr uint8_t CmdSetDioIrqParams = 0x08;
constexpr uint8_t CmdGetIrqStatus = 0x12;
constexpr uint8_t CmdClearIrqStatus = 0x02;
constexpr uint8_t CmdGetPacketStatus = 0x14;
constexpr uint8_t CmdGetRxBufferStatus = 0x13;
constexpr uint8_t CmdReadBuffer = 0x1E;
constexpr uint8_t CmdWriteBuffer = 0x0E;
constexpr uint8_t CmdSetDio2AsRfSwitchCtrl = 0x9D;
constexpr uint8_t CmdReadRegister = 0x1D;
constexpr uint8_t CmdWriteRegister = 0x0D;

constexpr uint8_t PacketTypeLoRa = 0x01;
constexpr uint8_t StandbyRc = 0x00;
constexpr uint8_t RegulatorDcDc = 0x01;
constexpr uint8_t FallbackStandbyRc = 0x20;
constexpr uint8_t PaRamp200u = 0x04;
constexpr uint8_t DeviceSelSx1262 = 0x00;
constexpr uint8_t PaLut = 0x01;
constexpr uint8_t LoRaHeaderExplicit = 0x00;
constexpr uint8_t LoRaCrcOff = 0x00;
constexpr uint8_t LoRaCrcOn = 0x01;
constexpr uint8_t LoRaIqStandard = 0x00;

constexpr uint16_t IrqTxDone = 0x0001;
constexpr uint16_t IrqRxDone = 0x0002;
constexpr uint16_t IrqHeaderErr = 0x0020;
constexpr uint16_t IrqCrcErr = 0x0040;
constexpr uint16_t IrqTimeout = 0x0200;
constexpr uint16_t IrqAll = 0x43FF;

constexpr uint16_t RegOcpConfiguration = 0x08E7;
constexpr uint16_t RegLoRaSyncWordMsb = 0x0740;
constexpr uint16_t RegVersionString = 0x0320;

constexpr uint32_t RxTimeoutInfinite = 0x00FFFFFF;
constexpr uint32_t CrystalFrequencyHz = 32'000'000U;
constexpr uint32_t FrequencyStepShift = 25U;
constexpr TickType_t BusyTimeoutTicks = pdMS_TO_TICKS(50);
constexpr TickType_t IrqPollStepTicks = pdMS_TO_TICKS(2);
constexpr TickType_t TxDoneTimeoutTicks = pdMS_TO_TICKS(4000);

class SpiControllerGuard {
    ::Device* controller = nullptr;
    bool locked = false;

public:
    explicit SpiControllerGuard(::Device* controller) : controller(controller) {
        if (controller != nullptr) {
            locked = spi_controller_lock(controller) == ERROR_NONE;
        }
    }

    ~SpiControllerGuard() {
        if (locked && controller != nullptr) {
            spi_controller_unlock(controller);
        }
    }

    bool ok() const { return locked; }
};

uint8_t mapBandwidth(uint32_t bandwidthHz) {
    switch (bandwidthHz) {
        case 7'800U: return 0x00;
        case 10'400U: return 0x08;
        case 15'600U: return 0x01;
        case 20'800U: return 0x09;
        case 31'250U: return 0x02;
        case 41'700U: return 0x0A;
        case 62'500U: return 0x03;
        case 125'000U: return 0x04;
        case 250'000U: return 0x05;
        case 500'000U: return 0x06;
        default: return 0x04;
    }
}

uint8_t mapCodingRate(uint8_t codingRate) {
    if (codingRate < 5U) {
        return 0x01;
    }
    if (codingRate > 8U) {
        return 0x04;
    }
    return static_cast<uint8_t>(codingRate - 4U);
}

uint8_t calcLowDataRateOptimization(uint8_t spreadingFactor, uint32_t bandwidthHz) {
    const auto symbolMilliseconds =
        static_cast<double>(1U << spreadingFactor) / (static_cast<double>(bandwidthHz) / 1000.0);
    return symbolMilliseconds >= 16.0 ? 0x01 : 0x00;
}

std::array<uint8_t, 2> imageCalibrationData(uint32_t frequencyHz) {
    if (frequencyHz < 779'000'000U) {
        return { 0xC1, 0xC5 };
    }
    if (frequencyHz < 902'000'000U) {
        return { 0xD7, 0xDB };
    }
    return { 0xE1, 0xE9 };
}

uint32_t rfFrequencyRaw(uint32_t frequencyHz) {
    return static_cast<uint32_t>((static_cast<uint64_t>(frequencyHz) << FrequencyStepShift) / CrystalFrequencyHz);
}

int32_t calculateBitrate(const tt::hal::radio::LoRaConfiguration& configuration) {
    if (configuration.bandwidth == 0U || configuration.spreadingFactor == 0U || configuration.codingRate == 0U) {
        return 0;
    }

    const auto denominator =
        std::pow(2.0, static_cast<double>(configuration.spreadingFactor)) /
        (static_cast<double>(configuration.bandwidth) / 1000.0);
    if (denominator <= 0.0) {
        return 0;
    }

    const auto bitrate = static_cast<int32_t>(std::round(
        static_cast<double>(configuration.spreadingFactor) *
        ((4.0 / static_cast<double>(configuration.codingRate)) / denominator) *
        1000.0
    ));
    return std::max<int32_t>(bitrate, 0);
}

bool versionLooksValid(const std::array<uint8_t, 6>& versionBytes) {
    const bool allZero = std::all_of(versionBytes.begin(), versionBytes.end(), [](uint8_t value) {
        return value == 0x00;
    });
    const bool allOnes = std::all_of(versionBytes.begin(), versionBytes.end(), [](uint8_t value) {
        return value == 0xFF;
    });
    return !allZero && !allOnes;
}

std::string formatDetail(const tt::hal::radio::LoRaConfiguration& configuration) {
    return std::format(
        "SX1262 online on spi0 at {} Hz, BW {} Hz, SF{}, CR 4/{}",
        configuration.frequency,
        configuration.bandwidth,
        configuration.spreadingFactor,
        configuration.codingRate
    );
}

} // namespace

bool TdeckSx1262LoRaDevice::resolveSpiControllerLocked() {
    if (spiController != nullptr) {
        return true;
    }

    spiController = device_find_by_name("spi0");
    if (spiController == nullptr) {
        detail = "SPI controller spi0 is not available";
        LOGGER.error("{}", detail);
        return false;
    }

    const auto* config = static_cast<const Esp32SpiConfig*>(spiController->config);
    if (config == nullptr) {
        detail = "spi0 config is missing";
        LOGGER.error("{}", detail);
        spiController = nullptr;
        return false;
    }

    spiHost = config->host;
    return true;
}

bool TdeckSx1262LoRaDevice::configurePinsLocked() {
    gpio_config_t resetConfig = {};
    resetConfig.pin_bit_mask = 1ULL << static_cast<uint32_t>(TdeckLoRaResetPin);
    resetConfig.mode = GPIO_MODE_OUTPUT;
    resetConfig.pull_up_en = GPIO_PULLUP_DISABLE;
    resetConfig.pull_down_en = GPIO_PULLDOWN_DISABLE;
    resetConfig.intr_type = GPIO_INTR_DISABLE;
    if (gpio_config(&resetConfig) != ESP_OK) {
        detail = "Failed to configure SX1262 reset pin";
        LOGGER.error("{}", detail);
        return false;
    }

    gpio_config_t busyConfig = {};
    busyConfig.pin_bit_mask = 1ULL << static_cast<uint32_t>(TdeckLoRaBusyPin);
    busyConfig.mode = GPIO_MODE_INPUT;
    busyConfig.pull_up_en = GPIO_PULLUP_DISABLE;
    busyConfig.pull_down_en = GPIO_PULLDOWN_DISABLE;
    busyConfig.intr_type = GPIO_INTR_DISABLE;
    if (gpio_config(&busyConfig) != ESP_OK) {
        detail = "Failed to configure SX1262 busy pin";
        LOGGER.error("{}", detail);
        return false;
    }

    gpio_config_t irqConfig = {};
    irqConfig.pin_bit_mask = 1ULL << static_cast<uint32_t>(TdeckLoRaIrqPin);
    irqConfig.mode = GPIO_MODE_INPUT;
    irqConfig.pull_up_en = GPIO_PULLUP_DISABLE;
    irqConfig.pull_down_en = GPIO_PULLDOWN_DISABLE;
    irqConfig.intr_type = GPIO_INTR_DISABLE;
    if (gpio_config(&irqConfig) != ESP_OK) {
        detail = "Failed to configure SX1262 irq pin";
        LOGGER.error("{}", detail);
        return false;
    }

    gpio_set_level(TdeckLoRaResetPin, 1);
    return true;
}

bool TdeckSx1262LoRaDevice::createSpiHandleLocked() {
    if (spiHandle != nullptr) {
        return true;
    }

    SpiControllerGuard guard(spiController);
    if (!guard.ok()) {
        detail = "Failed to lock spi0 for SX1262 setup";
        LOGGER.error("{}", detail);
        return false;
    }

    spi_device_interface_config_t deviceConfig = {};
    deviceConfig.mode = 0;
    deviceConfig.clock_speed_hz = SpiClockHz;
    deviceConfig.spics_io_num = TdeckLoRaChipSelectPin;
    deviceConfig.queue_size = 1;

    if (spi_bus_add_device(spiHost, &deviceConfig, &spiHandle) != ESP_OK) {
        detail = "Failed to create SX1262 SPI device handle";
        LOGGER.error("{}", detail);
        spiHandle = nullptr;
        return false;
    }

    return true;
}

void TdeckSx1262LoRaDevice::removeSpiHandleLocked() {
    if (spiHandle == nullptr || spiController == nullptr) {
        spiHandle = nullptr;
        return;
    }

    SpiControllerGuard guard(spiController);
    if (!guard.ok()) {
        LOGGER.warn("Failed to lock spi0 while removing SX1262 SPI device handle");
        return;
    }

    if (spi_bus_remove_device(spiHandle) != ESP_OK) {
        LOGGER.warn("Failed to remove SX1262 SPI device handle cleanly");
        return;
    }

    spiHandle = nullptr;
}

void TdeckSx1262LoRaDevice::shutdownLocked() {
    if (spiHandle != nullptr && spiController != nullptr) {
        SpiControllerGuard guard(spiController);
        if (guard.ok()) {
            const uint8_t standbyMode = StandbyRc;
            writeCommandOnBusLocked(CmdSetStandby, &standbyMode, 1, true);
            clearIrqOnBusLocked(IrqAll);
        }
    }

    removeSpiHandleLocked();
    receiveCallback = nullptr;
    started = false;
    metrics.available = false;
    metrics.rssi = 0;
    metrics.snr = 0;
    detail = "SX1262 backend is offline";
}

bool TdeckSx1262LoRaDevice::waitWhileBusy(TickType_t timeout) const {
    const TickType_t deadline = xTaskGetTickCount() + timeout;
    while (gpio_get_level(TdeckLoRaBusyPin) != 0) {
        if (xTaskGetTickCount() >= deadline) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return true;
}

bool TdeckSx1262LoRaDevice::waitForIrqHigh(TickType_t timeout) const {
    const TickType_t deadline = xTaskGetTickCount() + timeout;
    while (gpio_get_level(TdeckLoRaIrqPin) == 0) {
        if (timeout == 0 || xTaskGetTickCount() >= deadline) {
            return false;
        }
        vTaskDelay(IrqPollStepTicks);
    }
    return true;
}

bool TdeckSx1262LoRaDevice::writeCommandOnBusLocked(uint8_t command, const uint8_t* data, size_t size, bool waitReady) {
    if (spiHandle == nullptr) {
        return false;
    }

    if (!waitWhileBusy(BusyTimeoutTicks)) {
        detail = "SX1262 busy pin remained asserted before command write";
        return false;
    }

    std::array<uint8_t, 260> tx = {};
    const auto total = size + 1U;
    if (total > tx.size()) {
        detail = "SX1262 command payload too large";
        return false;
    }

    tx[0] = command;
    if (data != nullptr && size > 0) {
        std::memcpy(tx.data() + 1, data, size);
    }

    spi_transaction_t transaction = {};
    transaction.length = static_cast<uint32_t>(total * 8U);
    transaction.tx_buffer = tx.data();
    if (spi_device_transmit(spiHandle, &transaction) != ESP_OK) {
        detail = "SX1262 SPI command write failed";
        return false;
    }

    return !waitReady || waitWhileBusy(BusyTimeoutTicks);
}

bool TdeckSx1262LoRaDevice::readCommandOnBusLocked(
    uint8_t command,
    const uint8_t* prefix,
    size_t prefixSize,
    uint8_t* data,
    size_t size,
    bool waitReady
) {
    if (spiHandle == nullptr) {
        return false;
    }

    if (!waitWhileBusy(BusyTimeoutTicks)) {
        detail = "SX1262 busy pin remained asserted before command read";
        return false;
    }

    std::array<uint8_t, 260> tx = {};
    std::array<uint8_t, 260> rx = {};
    const auto total = 1U + prefixSize + 1U + size;
    if (total > tx.size()) {
        detail = "SX1262 read command payload too large";
        return false;
    }

    tx[0] = command;
    if (prefix != nullptr && prefixSize > 0) {
        std::memcpy(tx.data() + 1, prefix, prefixSize);
    }

    spi_transaction_t transaction = {};
    transaction.length = static_cast<uint32_t>(total * 8U);
    transaction.tx_buffer = tx.data();
    transaction.rx_buffer = rx.data();
    if (spi_device_transmit(spiHandle, &transaction) != ESP_OK) {
        detail = "SX1262 SPI command read failed";
        return false;
    }

    if (data != nullptr && size > 0) {
        std::memcpy(data, rx.data() + 1 + prefixSize + 1, size);
    }

    return !waitReady || waitWhileBusy(BusyTimeoutTicks);
}

bool TdeckSx1262LoRaDevice::writeRegisterOnBusLocked(uint16_t address, const uint8_t* data, size_t size) {
    std::array<uint8_t, 2> prefix = {
        static_cast<uint8_t>((address >> 8) & 0xFF),
        static_cast<uint8_t>(address & 0xFF)
    };

    std::array<uint8_t, 260> tx = {};
    const auto total = 1U + prefix.size() + size;
    if (total > tx.size()) {
        detail = "SX1262 register write payload too large";
        return false;
    }

    tx[0] = CmdWriteRegister;
    std::memcpy(tx.data() + 1, prefix.data(), prefix.size());
    if (data != nullptr && size > 0) {
        std::memcpy(tx.data() + 1 + prefix.size(), data, size);
    }

    if (!waitWhileBusy(BusyTimeoutTicks)) {
        detail = "SX1262 busy pin remained asserted before register write";
        return false;
    }

    spi_transaction_t transaction = {};
    transaction.length = static_cast<uint32_t>(total * 8U);
    transaction.tx_buffer = tx.data();
    if (spi_device_transmit(spiHandle, &transaction) != ESP_OK) {
        detail = "SX1262 register write failed";
        return false;
    }

    return waitWhileBusy(BusyTimeoutTicks);
}

bool TdeckSx1262LoRaDevice::readRegisterOnBusLocked(uint16_t address, uint8_t* data, size_t size) {
    std::array<uint8_t, 2> prefix = {
        static_cast<uint8_t>((address >> 8) & 0xFF),
        static_cast<uint8_t>(address & 0xFF)
    };
    return readCommandOnBusLocked(CmdReadRegister, prefix.data(), prefix.size(), data, size, true);
}

bool TdeckSx1262LoRaDevice::clearIrqOnBusLocked(uint16_t flags) {
    std::array<uint8_t, 2> data = {
        static_cast<uint8_t>((flags >> 8) & 0xFF),
        static_cast<uint8_t>(flags & 0xFF)
    };
    return writeCommandOnBusLocked(CmdClearIrqStatus, data.data(), data.size(), true);
}

bool TdeckSx1262LoRaDevice::readIrqStatusOnBusLocked(uint16_t& flags) {
    std::array<uint8_t, 2> data = {};
    if (!readCommandOnBusLocked(CmdGetIrqStatus, nullptr, 0, data.data(), data.size(), true)) {
        return false;
    }

    flags = static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) | data[1]);
    return true;
}

bool TdeckSx1262LoRaDevice::setRfFrequencyOnBusLocked(uint32_t frequencyHz) {
    const auto calibration = imageCalibrationData(frequencyHz);
    if (!writeCommandOnBusLocked(CmdCalibrateImage, calibration.data(), calibration.size(), true)) {
        return false;
    }

    const auto raw = rfFrequencyRaw(frequencyHz);
    std::array<uint8_t, 4> data = {
        static_cast<uint8_t>((raw >> 24) & 0xFF),
        static_cast<uint8_t>((raw >> 16) & 0xFF),
        static_cast<uint8_t>((raw >> 8) & 0xFF),
        static_cast<uint8_t>(raw & 0xFF)
    };
    return writeCommandOnBusLocked(CmdSetRfFrequency, data.data(), data.size(), true);
}

bool TdeckSx1262LoRaDevice::setTxPowerOnBusLocked(uint8_t txPower) {
    const int8_t clipped = static_cast<int8_t>(std::clamp<int>(txPower, -9, 22));
    uint8_t savedOcp = 0;
    readRegisterOnBusLocked(RegOcpConfiguration, &savedOcp, 1);

    const std::array<uint8_t, 4> paConfig = { 0x04, 0x07, DeviceSelSx1262, PaLut };
    if (!writeCommandOnBusLocked(CmdSetPaConfig, paConfig.data(), paConfig.size(), true)) {
        return false;
    }

    const std::array<uint8_t, 2> txParams = { static_cast<uint8_t>(clipped), PaRamp200u };
    const bool ok = writeCommandOnBusLocked(CmdSetTxParams, txParams.data(), txParams.size(), true);
    writeRegisterOnBusLocked(RegOcpConfiguration, &savedOcp, 1);
    return ok;
}

bool TdeckSx1262LoRaDevice::setPacketParamsOnBusLocked(uint8_t payloadLength) {
    const std::array<uint8_t, 6> data = {
        static_cast<uint8_t>((activeConfiguration.preambleLength >> 8) & 0xFF),
        static_cast<uint8_t>(activeConfiguration.preambleLength & 0xFF),
        LoRaHeaderExplicit,
        payloadLength,
        activeConfiguration.crc ? LoRaCrcOn : LoRaCrcOff,
        LoRaIqStandard
    };
    return writeCommandOnBusLocked(CmdSetPacketParams, data.data(), data.size(), true);
}

bool TdeckSx1262LoRaDevice::resetRadioOnBusLocked() {
    gpio_set_level(TdeckLoRaResetPin, 1);
    vTaskDelay(pdMS_TO_TICKS(2));
    gpio_set_level(TdeckLoRaResetPin, 0);
    vTaskDelay(pdMS_TO_TICKS(2));
    gpio_set_level(TdeckLoRaResetPin, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    if (!waitWhileBusy(pdMS_TO_TICKS(100))) {
        detail = "SX1262 busy pin did not go idle after reset";
        return false;
    }
    return true;
}

bool TdeckSx1262LoRaDevice::probeRadioOnBusLocked() {
    std::array<uint8_t, 6> versionBytes = {};
    if (!readRegisterOnBusLocked(RegVersionString, versionBytes.data(), versionBytes.size())) {
        detail = "SX1262 version register probe failed";
        return false;
    }

    if (!versionLooksValid(versionBytes)) {
        detail = "SX1262 probe returned invalid version bytes";
        return false;
    }

    return true;
}

bool TdeckSx1262LoRaDevice::initializeRadioOnBusLocked() {
    const uint8_t calibrate = 0x7F;
    if (!writeCommandOnBusLocked(CmdCalibrate, &calibrate, 1, true)) {
        return false;
    }

    const uint8_t packetType = PacketTypeLoRa;
    if (!writeCommandOnBusLocked(CmdSetPacketType, &packetType, 1, true)) {
        return false;
    }

    const std::array<uint8_t, 2> bufferBase = { 0x00, 0x00 };
    if (!writeCommandOnBusLocked(CmdSetBufferBaseAddress, bufferBase.data(), bufferBase.size(), true)) {
        return false;
    }

    const uint8_t regulator = RegulatorDcDc;
    if (!writeCommandOnBusLocked(CmdSetRegulatorMode, &regulator, 1, true)) {
        return false;
    }

    const uint8_t dio2RfSwitch = 0x01;
    if (!writeCommandOnBusLocked(CmdSetDio2AsRfSwitchCtrl, &dio2RfSwitch, 1, true)) {
        return false;
    }

    const uint8_t fallback = FallbackStandbyRc;
    if (!writeCommandOnBusLocked(CmdSetRxTxFallbackMode, &fallback, 1, true)) {
        return false;
    }

    if (!clearIrqOnBusLocked(IrqAll)) {
        return false;
    }

    const uint8_t ocp = static_cast<uint8_t>(60.0f / 2.5f);
    return writeRegisterOnBusLocked(RegOcpConfiguration, &ocp, 1);
}

bool TdeckSx1262LoRaDevice::configureLoRaOnBusLocked(const tt::hal::radio::LoRaConfiguration& configuration) {
    activeConfiguration = configuration;

    const uint8_t standbyMode = StandbyRc;
    if (!writeCommandOnBusLocked(CmdSetStandby, &standbyMode, 1, true)) {
        return false;
    }

    const uint8_t packetType = PacketTypeLoRa;
    if (!writeCommandOnBusLocked(CmdSetPacketType, &packetType, 1, true)) {
        return false;
    }

    if (!setRfFrequencyOnBusLocked(configuration.frequency) || !setTxPowerOnBusLocked(configuration.txPower)) {
        return false;
    }

    const std::array<uint8_t, 4> modulationParams = {
        configuration.spreadingFactor,
        mapBandwidth(configuration.bandwidth),
        mapCodingRate(configuration.codingRate),
        calcLowDataRateOptimization(configuration.spreadingFactor, configuration.bandwidth)
    };
    if (!writeCommandOnBusLocked(CmdSetModulationParams, modulationParams.data(), modulationParams.size(), true)) {
        return false;
    }

    if (!setPacketParamsOnBusLocked(0xFF)) {
        return false;
    }

    const std::array<uint8_t, 2> syncWord = {
        static_cast<uint8_t>((configuration.syncWord & 0xF0) | 0x04),
        static_cast<uint8_t>(((configuration.syncWord & 0x0F) << 4) | 0x04)
    };
    return writeRegisterOnBusLocked(RegLoRaSyncWordMsb, syncWord.data(), syncWord.size());
}

bool TdeckSx1262LoRaDevice::setReceiveModeOnBusLocked() {
    const std::array<uint8_t, 8> irqConfig = {
        static_cast<uint8_t>(((IrqRxDone | IrqTimeout | IrqCrcErr | IrqHeaderErr) >> 8) & 0xFF),
        static_cast<uint8_t>((IrqRxDone | IrqTimeout | IrqCrcErr | IrqHeaderErr) & 0xFF),
        static_cast<uint8_t>((IrqRxDone >> 8) & 0xFF),
        static_cast<uint8_t>(IrqRxDone & 0xFF),
        0x00,
        0x00,
        0x00,
        0x00
    };
    if (!writeCommandOnBusLocked(CmdSetDioIrqParams, irqConfig.data(), irqConfig.size(), true)) {
        return false;
    }

    if (!clearIrqOnBusLocked(IrqAll)) {
        return false;
    }

    if (!setPacketParamsOnBusLocked(0xFF)) {
        return false;
    }

    const std::array<uint8_t, 2> bufferBase = { 0x00, 0x00 };
    if (!writeCommandOnBusLocked(CmdSetBufferBaseAddress, bufferBase.data(), bufferBase.size(), true)) {
        return false;
    }

    const std::array<uint8_t, 3> timeout = {
        static_cast<uint8_t>((RxTimeoutInfinite >> 16) & 0xFF),
        static_cast<uint8_t>((RxTimeoutInfinite >> 8) & 0xFF),
        static_cast<uint8_t>(RxTimeoutInfinite & 0xFF)
    };
    return writeCommandOnBusLocked(CmdSetRx, timeout.data(), timeout.size(), true);
}

bool TdeckSx1262LoRaDevice::readPacketOnBusLocked(std::vector<uint8_t>& payload) {
    std::array<uint8_t, 2> bufferStatus = {};
    if (!readCommandOnBusLocked(CmdGetRxBufferStatus, nullptr, 0, bufferStatus.data(), bufferStatus.size(), true)) {
        return false;
    }

    const auto packetLength = std::min<size_t>(bufferStatus[0], PacketBufferSize);
    payload.assign(packetLength, 0);
    if (packetLength == 0U) {
        return true;
    }

    const uint8_t offset = bufferStatus[1];
    return readCommandOnBusLocked(CmdReadBuffer, &offset, 1, payload.data(), payload.size(), true);
}

bool TdeckSx1262LoRaDevice::readPacketStatusOnBusLocked() {
    std::array<uint8_t, 3> packetStatus = {};
    if (!readCommandOnBusLocked(CmdGetPacketStatus, nullptr, 0, packetStatus.data(), packetStatus.size(), true)) {
        return false;
    }

    metrics.rssi = static_cast<int16_t>(-static_cast<int16_t>(packetStatus[0]) / 2);
    metrics.snr = static_cast<int16_t>(std::lround(static_cast<double>(static_cast<int8_t>(packetStatus[1])) / 4.0));
    return true;
}

void TdeckSx1262LoRaDevice::updateMetricsLocked() {
    metrics.available = started;
    metrics.hardwareMtu = PacketBufferSize;
    metrics.bitrate = calculateBitrate(activeConfiguration);
}

bool TdeckSx1262LoRaDevice::handleIrqOnBusLocked(std::vector<uint8_t>& payloadToDeliver, bool& receivedPacket) {
    receivedPacket = false;

    uint16_t irqFlags = 0;
    if (!readIrqStatusOnBusLocked(irqFlags)) {
        return false;
    }

    if (irqFlags == 0U) {
        return true;
    }

    if (!clearIrqOnBusLocked(irqFlags)) {
        return false;
    }

    if ((irqFlags & IrqRxDone) != 0U) {
        if (!readPacketStatusOnBusLocked() || !readPacketOnBusLocked(payloadToDeliver)) {
            return false;
        }
        receivedPacket = true;
    } else if ((irqFlags & IrqTxDone) != 0U) {
        LOGGER.debug("SX1262 transmit completed");
    } else if ((irqFlags & IrqTimeout) != 0U) {
        LOGGER.warn("SX1262 reported RX/TX timeout");
    } else if ((irqFlags & IrqCrcErr) != 0U) {
        LOGGER.warn("SX1262 reported CRC error");
    } else if ((irqFlags & IrqHeaderErr) != 0U) {
        LOGGER.warn("SX1262 reported header error");
    }

    return setReceiveModeOnBusLocked();
}

bool TdeckSx1262LoRaDevice::start(const tt::hal::radio::LoRaConfiguration& configuration, ReceiveCallback callback) {
    auto lock = mutex.asScopedLock();
    lock.lock();

    shutdownLocked();
    metrics = {};
    metrics.hardwareMtu = PacketBufferSize;
    receiveCallback = std::move(callback);

    if (!resolveSpiControllerLocked() || !configurePinsLocked() || !createSpiHandleLocked()) {
        return false;
    }

    SpiControllerGuard guard(spiController);
    if (!guard.ok()) {
        detail = "Failed to lock spi0 for SX1262 bring-up";
        LOGGER.error("{}", detail);
        return false;
    }

    if (!resetRadioOnBusLocked() || !probeRadioOnBusLocked() || !initializeRadioOnBusLocked() ||
        !configureLoRaOnBusLocked(configuration) || !setReceiveModeOnBusLocked()) {
        LOGGER.error("SX1262 bring-up failed: {}", detail);
        shutdownLocked();
        return false;
    }

    started = true;
    detail = formatDetail(configuration);
    updateMetricsLocked();
    LOGGER.info("{}", detail);
    return true;
}

void TdeckSx1262LoRaDevice::stop() {
    auto lock = mutex.asScopedLock();
    lock.lock();
    shutdownLocked();
}

bool TdeckSx1262LoRaDevice::poll(TickType_t timeout) {
    std::vector<uint8_t> payload;
    ReceiveCallback callback;
    bool receivedPacket = false;

    {
        auto lock = mutex.asScopedLock();
        lock.lock();
        if (!started) {
            return false;
        }

        if (!waitForIrqHigh(timeout)) {
            return true;
        }

        SpiControllerGuard guard(spiController);
        if (!guard.ok()) {
            detail = "Failed to lock spi0 for SX1262 IRQ handling";
            metrics.available = false;
            LOGGER.error("{}", detail);
            return false;
        }

        if (!handleIrqOnBusLocked(payload, receivedPacket)) {
            metrics.available = false;
            LOGGER.error("SX1262 IRQ handling failed: {}", detail);
            return false;
        }

        callback = receiveCallback;
        updateMetricsLocked();
    }

    if (receivedPacket && callback != nullptr && !payload.empty()) {
        callback(std::move(payload));
    }

    return true;
}

bool TdeckSx1262LoRaDevice::send(const std::vector<uint8_t>& payload) {
    auto lock = mutex.asScopedLock();
    lock.lock();

    if (!started) {
        LOGGER.warn("SX1262 send requested while backend is offline");
        return false;
    }

    if (payload.empty() || payload.size() > PacketBufferSize) {
        LOGGER.warn("SX1262 payload size {} is invalid", payload.size());
        return false;
    }

    {
        SpiControllerGuard guard(spiController);
        if (!guard.ok()) {
            detail = "Failed to lock spi0 for SX1262 transmit";
            LOGGER.error("{}", detail);
            return false;
        }

        const uint8_t standbyMode = StandbyRc;
        if (!writeCommandOnBusLocked(CmdSetStandby, &standbyMode, 1, true) ||
            !setPacketParamsOnBusLocked(static_cast<uint8_t>(payload.size()))) {
            LOGGER.error("SX1262 transmit preparation failed: {}", detail);
            return false;
        }

        std::array<uint8_t, PacketBufferSize + 2> writeBuffer = {};
        writeBuffer[0] = CmdWriteBuffer;
        writeBuffer[1] = 0x00;
        std::memcpy(writeBuffer.data() + 2, payload.data(), payload.size());

        spi_transaction_t transaction = {};
        transaction.length = static_cast<uint32_t>((payload.size() + 2U) * 8U);
        transaction.tx_buffer = writeBuffer.data();
        if (spi_device_transmit(spiHandle, &transaction) != ESP_OK) {
            detail = "SX1262 payload buffer write failed";
            LOGGER.error("{}", detail);
            return false;
        }

        const std::array<uint8_t, 8> irqConfig = {
            static_cast<uint8_t>(((IrqTxDone | IrqTimeout) >> 8) & 0xFF),
            static_cast<uint8_t>((IrqTxDone | IrqTimeout) & 0xFF),
            static_cast<uint8_t>((IrqTxDone >> 8) & 0xFF),
            static_cast<uint8_t>(IrqTxDone & 0xFF),
            0x00,
            0x00,
            0x00,
            0x00
        };
        if (!writeCommandOnBusLocked(CmdSetDioIrqParams, irqConfig.data(), irqConfig.size(), true) ||
            !clearIrqOnBusLocked(IrqAll)) {
            LOGGER.error("SX1262 transmit IRQ setup failed: {}", detail);
            return false;
        }

        const std::array<uint8_t, 3> timeoutRaw = { 0x00, 0x00, 0x00 };
        if (!writeCommandOnBusLocked(CmdSetTx, timeoutRaw.data(), timeoutRaw.size(), true)) {
            LOGGER.error("SX1262 transmit start failed: {}", detail);
            return false;
        }
    }

    if (!waitForIrqHigh(TxDoneTimeoutTicks)) {
        detail = "SX1262 transmit timed out waiting for IRQ";
        LOGGER.error("{}", detail);
        return false;
    }

    {
        SpiControllerGuard guard(spiController);
        if (!guard.ok()) {
            detail = "Failed to lock spi0 while finalizing SX1262 transmit";
            LOGGER.error("{}", detail);
            return false;
        }

        uint16_t irqFlags = 0;
        if (!readIrqStatusOnBusLocked(irqFlags) || !clearIrqOnBusLocked(irqFlags)) {
            LOGGER.error("SX1262 transmit completion read failed: {}", detail);
            return false;
        }

        if ((irqFlags & IrqTxDone) == 0U) {
            detail = std::format("SX1262 transmit did not finish cleanly (irq=0x{:04X})", irqFlags);
            LOGGER.error("{}", detail);
            setReceiveModeOnBusLocked();
            return false;
        }

        if (!setReceiveModeOnBusLocked()) {
            LOGGER.error("SX1262 failed to return to receive mode: {}", detail);
            return false;
        }
    }

    updateMetricsLocked();
    return true;
}

tt::hal::radio::LoRaMetrics TdeckSx1262LoRaDevice::getMetrics() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return metrics;
}

std::string TdeckSx1262LoRaDevice::getDetail() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return detail;
}

std::shared_ptr<tt::hal::Device> createLoRaDevice() {
    return std::make_shared<TdeckSx1262LoRaDevice>();
}
