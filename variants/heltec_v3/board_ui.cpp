// MeshRoute — src/board_ui.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §featuresplit slice 4: the board-UI implementation, compiled ONLY when MR_FEAT_OLED=1 (currently the heltec_v3
// profile). This is the EMPTY SEAM — the three mr_ui hooks link but do nothing yet; the next board-UI PR fills the
// bodies (bring up the on-board SSD1306 over I2C, draw identity/status, surface deliveries). Kept driver-free so the
// heltec build links today without pulling a display library. On every OTHER profile MR_FEAT_OLED=0 -> this whole TU
// is empty and fw_main uses the header's inline no-ops.
#include "mr_features.h"

#if MR_FEAT_OLED

#include "mr_ui.h"
#include "command.h"   // meshroute::Push (only the hook below takes it by const-ref; the fill-in reads pu.kind/origin/body)

// TODO(board-ui): I2C-init the on-board SSD1306 (heltec_v3: SDA=17 SCL=18 RST=21, 0x3C) and paint a splash.
// The implementation pulls the live node state itself (extern the g_node in fw_main, or a small accessor) — this
// header stays dependency-light on purpose.
void mr_ui_init() {
}

// TODO(board-ui): periodic refresh. now_ms is the mesh-service clock; THROTTLE here (e.g. repaint at <= 2 Hz) so the
// display work never steals the radio hot path. Draw: node id / leaf, registration or gateway role, last RX, RSSI.
void mr_ui_tick(uint32_t /*now_ms*/) {
}

// TODO(board-ui): surface an app Push (pu.kind: msg_recv / channel_recv / send_acked / send_failed / send_e2e_acked).
// e.g. flash a RX banner with pu.origin + the first bytes of pu.body, or a FAILED indicator with pu.reason.
void mr_ui_on_push(const meshroute::Push& /*pu*/) {
}

#endif  // MR_FEAT_OLED
