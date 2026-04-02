#pragma once

#include <Tactility/Mutex.h>
#include <Tactility/Thread.h>
#include <Tactility/service/reticulum/Interface.h>
#include <Tactility/settings/ReticulumSettings.h>

#include <memory>
#include <vector>

struct Device;

namespace tt::service::reticulum::interfaces {

class LoRaInterface final : public Interface {

    struct Kiss {
        static constexpr uint8_t Fend = 0xC0;
        static constexpr uint8_t Fesc = 0xDB;
        static constexpr uint8_t Tfend = 0xDC;
        static constexpr uint8_t Tfesc = 0xDD;

        static constexpr uint8_t CmdUnknown = 0xFE;
        static constexpr uint8_t CmdData = 0x00;
        static constexpr uint8_t CmdFrequency = 0x01;
        static constexpr uint8_t CmdBandwidth = 0x02;
        static constexpr uint8_t CmdTxPower = 0x03;
        static constexpr uint8_t CmdSf = 0x04;
        static constexpr uint8_t CmdCr = 0x05;
        static constexpr uint8_t CmdRadioState = 0x06;
        static constexpr uint8_t CmdRadioLock = 0x07;
        static constexpr uint8_t CmdDetect = 0x08;
        static constexpr uint8_t CmdLeave = 0x0A;
        static constexpr uint8_t CmdReady = 0x0F;
        static constexpr uint8_t CmdStatRssi = 0x23;
        static constexpr uint8_t CmdStatSnr = 0x24;
        static constexpr uint8_t CmdFwVersion = 0x50;
        static constexpr uint8_t CmdPlatform = 0x48;
        static constexpr uint8_t CmdMcu = 0x49;
        static constexpr uint8_t CmdError = 0x90;

        static constexpr uint8_t DetectRequest = 0x73;
        static constexpr uint8_t DetectResponse = 0x46;
        static constexpr uint8_t RadioStateOff = 0x00;
        static constexpr uint8_t RadioStateOn = 0x01;
    };

    static constexpr size_t HardwareMtu = 508;
    static constexpr auto* PreferredBackendDevice = "uart1";
    static constexpr uint32_t BackendBaudRate = 115200;

    ReceiveCallback receiveCallback;
    settings::reticulum::LoRaSettings settings;
    std::unique_ptr<Thread> thread;
    mutable Mutex mutex;
    ::Device* uart = nullptr;
    std::string backendDeviceName;
    bool online = false;
    bool threadInterrupted = false;
    bool detected = false;
    bool interfaceReady = false;
    bool firmwareOk = false;
    uint8_t firmwareMajor = 0;
    uint8_t firmwareMinor = 0;
    uint32_t reportedFrequency = 0;
    uint32_t reportedBandwidth = 0;
    uint8_t reportedTxPower = 0;
    uint8_t reportedSf = 0;
    uint8_t reportedCr = 0;
    uint8_t reportedRadioState = Kiss::RadioStateOff;
    InterfaceMetrics metrics {};
    std::vector<std::vector<uint8_t>> packetQueue {};

    bool resolveBackendDevice();
    bool openUart();
    void closeUart();
    bool configureDevice();
    bool validateConfiguredRadio() const;
    int32_t readLoop();
    bool writeFrameBytes(const std::vector<uint8_t>& bytes);
    bool writeKissCommand(uint8_t command, const std::vector<uint8_t>& data = {});
    void processReceivedPacket(const std::vector<uint8_t>& payload);
    void processQueue();
    void updateBitrateLocked();
    static std::vector<uint8_t> escape(const std::vector<uint8_t>& data);

public:

    explicit LoRaInterface(settings::reticulum::LoRaSettings settings = settings::reticulum::getDefault()) :
        settings(std::move(settings))
    {}

    std::string getId() const override { return "lora"; }

    InterfaceKind getKind() const override { return InterfaceKind::LoRa; }

    uint32_t getCapabilities() const override {
        return toMask(InterfaceCapability::Transport) |
            InterfaceCapability::Link |
            InterfaceCapability::Resource |
            InterfaceCapability::Metrics;
    }

    InterfaceMetrics getMetrics() const override;

    void setReceiveCallback(ReceiveCallback callback) override { receiveCallback = std::move(callback); }

    bool start() override;

    void stop() override;

    bool sendFrame(const InterfaceFrame& frame) override;
};

} // namespace tt::service::reticulum::interfaces
