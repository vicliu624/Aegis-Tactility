#pragma once

#include <Tactility/Mutex.h>
#include <Tactility/hal/radio/LoRaDevice.h>

#include <driver/spi_master.h>

#include <memory>
#include <string>
#include <vector>

struct Device;

class TdeckSx1262LoRaDevice final : public tt::hal::radio::LoRaDevice {
    mutable tt::Mutex mutex;
    ::Device* spiController = nullptr;
    spi_host_device_t spiHost = SPI2_HOST;
    spi_device_handle_t spiHandle = nullptr;
    ReceiveCallback receiveCallback;
    tt::hal::radio::LoRaConfiguration activeConfiguration {};
    tt::hal::radio::LoRaMetrics metrics {};
    std::string detail = "SX1262 backend is offline";
    bool started = false;

    bool resolveSpiControllerLocked();
    bool configurePinsLocked();
    bool createSpiHandleLocked();
    void removeSpiHandleLocked();
    void shutdownLocked();

    bool resetRadioOnBusLocked();
    bool probeRadioOnBusLocked();
    bool initializeRadioOnBusLocked();
    bool configureLoRaOnBusLocked(const tt::hal::radio::LoRaConfiguration& configuration);
    bool setReceiveModeOnBusLocked();
    bool handleIrqOnBusLocked(std::vector<uint8_t>& payloadToDeliver, bool& receivedPacket);
    bool waitWhileBusy(TickType_t timeout) const;
    bool waitForIrqHigh(TickType_t timeout) const;

    bool writeCommandOnBusLocked(uint8_t command, const uint8_t* data, size_t size, bool waitReady = true);
    bool readCommandOnBusLocked(uint8_t command, const uint8_t* prefix, size_t prefixSize, uint8_t* data, size_t size, bool waitReady = true);
    bool writeRegisterOnBusLocked(uint16_t address, const uint8_t* data, size_t size);
    bool readRegisterOnBusLocked(uint16_t address, uint8_t* data, size_t size);
    bool clearIrqOnBusLocked(uint16_t flags);
    bool readIrqStatusOnBusLocked(uint16_t& flags);
    bool setRfFrequencyOnBusLocked(uint32_t frequencyHz);
    bool setTxPowerOnBusLocked(uint8_t txPower);
    bool setPacketParamsOnBusLocked(uint8_t payloadLength);
    bool readPacketOnBusLocked(std::vector<uint8_t>& payload);
    bool readPacketStatusOnBusLocked();
    void updateMetricsLocked();

public:
    std::string getName() const override { return "SX1262 LoRa"; }
    std::string getDescription() const override { return "Native SX1262 LoRa backend for LilyGO T-Deck"; }

    bool start(const tt::hal::radio::LoRaConfiguration& configuration, ReceiveCallback receiveCallback) override;
    void stop() override;
    bool poll(TickType_t timeout) override;
    bool send(const std::vector<uint8_t>& payload) override;
    tt::hal::radio::LoRaMetrics getMetrics() const override;
    std::string getDetail() const override;
};

std::shared_ptr<tt::hal::Device> createLoRaDevice();
