// Extend the shared radio fake with TX-byte capture and packet-in-progress carrier sense.
// Other platform behaviour remains in the base; RX mode alone is not an arriving packet.
#pragma once
#define CustomSX1262 MrInboxBaseSX1262
#include "../../../../probe_device_radio/fakes/helpers/radiolib/CustomSX1262.h"
#undef CustomSX1262
class CustomSX1262 : public MrInboxBaseSX1262 {
public:
    std::vector<std::vector<uint8_t>> tx_frames;
    bool packet_in_progress = false;
    bool isReceiving() {
        (void)MrInboxBaseSX1262::isReceiving(); // retain the shared fake's event capture
        return packet_in_progress; // RX mode enabled alone does not mean a packet is arriving
    }
    int16_t startTransmit(uint8_t* p, size_t n) {
        const int16_t result = MrInboxBaseSX1262::startTransmit(p, n);
        if (result == RADIOLIB_ERR_NONE) tx_frames.emplace_back(p, p + n);
        return result;
    }
};
