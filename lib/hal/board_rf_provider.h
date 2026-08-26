// MeshRoute — lib/hal/board_rf_provider.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Optional-capability provider, matching mr_ui.h's build split: a board with an external RF front end supplies the
// implementation in its variant TU; every existing board gets an inline null provider and keeps the direct-SX1262
// path. Keeping this decision here prevents board macros and FEM pins from leaking into fw_main.cpp.
#pragma once

#include "iboard_rf.h"

namespace meshroute {

#if defined(MR_BOARD_RF_FRONTEND) && MR_BOARD_RF_FRONTEND
IBoardRf* board_rf_instance();
#else
inline IBoardRf* board_rf_instance() { return nullptr; }
#endif

}  // namespace meshroute
