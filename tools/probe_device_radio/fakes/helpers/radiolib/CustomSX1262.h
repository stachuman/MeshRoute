// Counting CustomSX1262 stand-in for the V4-3 production-header probe. The class surface mirrors only what the real
// device_radio.h calls; it deliberately knows nothing about FEM sequencing, which must come from IBoardRf.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class CustomSX1262 {
public:
    std::vector<std::string> events;
    int16_t start_receive_result = RADIOLIB_ERR_NONE;
    int16_t start_transmit_result = RADIOLIB_ERR_NONE;
    int16_t set_frequency_result = RADIOLIB_ERR_NONE;
    int16_t read_result = RADIOLIB_ERR_NONE;
    uint16_t irq_flags = 0;
    size_t packet_length = 3;
    float packet_snr = 4.5f;
    float packet_rssi = -91.0f;
    bool receiving = false;
    void (*dio1_action)() = nullptr;
    int8_t last_power = -127;
    float last_frequency = 0.0f;

    void setPacketReceivedAction(void (*fn)()) { events.emplace_back("radio.set_action"); dio1_action = fn; }
    int16_t startReceive() {
        events.emplace_back("radio.start_rx");
        receiving = (start_receive_result == RADIOLIB_ERR_NONE);
        return start_receive_result;
    }
    int16_t standby() { events.emplace_back("radio.standby"); receiving = false; return RADIOLIB_ERR_NONE; }
    int16_t setSpreadingFactor(uint8_t sf) {
        events.emplace_back("radio.sf:" + std::to_string(sf)); return RADIOLIB_ERR_NONE;
    }
    int16_t setBandwidth(float) { events.emplace_back("radio.bw"); return RADIOLIB_ERR_NONE; }
    int16_t setCodingRate(uint8_t cr) {
        events.emplace_back("radio.cr:" + std::to_string(cr)); return RADIOLIB_ERR_NONE;
    }
    int16_t setOutputPower(int8_t power) {
        last_power = power;
        events.emplace_back("radio.power:" + std::to_string(static_cast<int>(power)));
        return RADIOLIB_ERR_NONE;
    }
    int16_t setPreambleLength(uint16_t preamble) {
        events.emplace_back("radio.pre:" + std::to_string(preamble)); return RADIOLIB_ERR_NONE;
    }
    int16_t startTransmit(uint8_t*, size_t) {
        events.emplace_back("radio.start_tx"); return start_transmit_result;
    }
    int16_t finishTransmit() { events.emplace_back("radio.finish_tx"); return RADIOLIB_ERR_NONE; }
    int16_t setFrequency(float mhz) {
        last_frequency = mhz;
        events.emplace_back("radio.freq");
        return set_frequency_result;
    }
    bool isReceiving() { events.emplace_back("radio.is_receiving"); return receiving; }
    float getRSSI(bool = true) { events.emplace_back("radio.rssi"); return packet_rssi; }
    float getSNR() { events.emplace_back("radio.snr"); return packet_snr; }
    uint16_t getIrqFlags() { events.emplace_back("radio.irq"); return irq_flags; }
    size_t getPacketLength() { events.emplace_back("radio.length"); return packet_length; }
    int16_t readData(uint8_t* out, size_t n) {
        events.emplace_back("radio.read");
        for (size_t i = 0; i < n; ++i) out[i] = static_cast<uint8_t>(i + 1);
        return read_result;
    }
};
