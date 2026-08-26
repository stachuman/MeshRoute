// MeshRoute — variants/heltec_v4/board_rf.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Pure half of the Heltec V4 FEM detector. Each bias is sampled twice: only a stable external pull that dominates
// both MCU pulls identifies a supported board. A floating or unstable discriminator is never guessed.
#pragma once

#include "iboard_rf.h"

namespace meshroute {

inline BoardRfKind classify_heltec_v4_fem(bool pullup_a, bool pullup_b,
                                           bool pulldown_a, bool pulldown_b) {
    if (!pullup_a && !pullup_b && !pulldown_a && !pulldown_b) return BoardRfKind::gc1109;
    if ( pullup_a &&  pullup_b &&  pulldown_a &&  pulldown_b) return BoardRfKind::kct8103l;
    return BoardRfKind::unknown;
}

}  // namespace meshroute
