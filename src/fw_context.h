// MeshRoute — src/fw_context.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The shared FIRMWARE CONTEXT: extern declarations of fw_main.cpp's device-stack + runtime/config/admin
// globals (cleanup 2026-07-14, codebase-review triage "split firmware by responsibility"). This is the
// FOUNDATION step — a pure static→extern seam with NO code moved — so the subsequent cluster extractions
// (firmware_remote / firmware_config / firmware_commands / transports) become straight moves onto a vetted base.
//
// The 1:1 rule (auditable): every mutable file-scope global that fw_main.cpp used to declare `static` is now
//   (a) `extern`-declared here, and (b) DEFINED once (non-static) in fw_main.cpp, guards matched.
// Excluded on purpose: the three `static constexpr` compile-time constants (MESHROUTE_SYNC_WORD,
// kChannelCtrLeaseMargin, REMOTE_FLAG_SEALED) stay in fw_main.cpp — they are constants, not shared state, and
// each moves WITH its cluster when that cluster is extracted.
//
// DEVICE-ONLY header (pulls RadioLib etc.) — included by fw_main.cpp and future device cluster TUs, never by
// the native unit suite.
#pragma once
#include <Arduino.h>
#include <RadioLib.h>
#include "helpers/radiolib/CustomSX1262.h"   // vendored — Module lives in RadioLib; CustomSX1262 here
#include <cstdint>
#include "protocol_constants.h"   // meshroute::protocol::max_payload_bytes_hard_cap (g_rxbuf bound)
#include "iclock.h"               // meshroute::ArduinoClock
#include "device_radio.h"         // meshroute::Sx1262Radio
#include "device_hal.h"           // meshroute::DeviceHal
#include "node.h"                 // meshroute::Node
#include "identity.h"             // meshroute::Identity
#include "device_inbox_store.h"   // mrinbox::DeviceInboxStore (nRF52 QSPIFLASH=1 => the LIVE QSPI/LittleFS backend)
#include "fixed_inbox_store.h"    // meshroute::FixedInboxStore (the ESP32 RAM-ring fallback)
#include "fault_log.h"            // mrfault::FaultLog / FaultRecord
#include "sched_send.h"           // mrsched::Schedule

// ---- persistent fault log + halt/remote-action/scheduled-send (fw_main.cpp block @61-75) ----
extern mrfault::FaultLog    g_fault_log;
extern mrfault::FaultRecord g_last_reset;
extern bool                 g_last_reset_valid;
extern bool                 g_halted;              // `prep-restart`: loop stays DORMANT (WDT-fed) but serves the console
extern uint8_t              g_remote_action;       // `rcmd` deferred recovery: 0=none 1=reboot 2=prep-restart
extern uint64_t             g_remote_action_at;
extern mrsched::Schedule    g_sched;               // firmware scheduled-send (testsend/testch) workload

// ---- the device stack (defined in fw_main.cpp; ctor args live with the definition) ----
extern Module                  g_mod;
extern CustomSX1262            g_radio;
extern meshroute::ArduinoClock g_clock;
extern meshroute::Sx1262Radio  g_iradio;
extern meshroute::DeviceHal    g_hal;
extern meshroute::Node         g_node;

// Inbox stores — guard MUST match the fw_main.cpp definitions: the durable QSPI/LittleFS DeviceInboxStore backend
// (nRF52, QSPIFLASH=1 => MRINBOX_QSPI_READY, the LIVE backend there) vs the FixedInboxStore RAM ring (ESP32 fallback).
// device_inbox_store.h's member defs are `inline`, so it is safe to include across TUs (fw_main + firmware_remote via here).
#ifndef MR_RAM_INBOX_SLOTS
#define MR_RAM_INBOX_SLOTS 32           // ESP32 RAM inbox depth per store (~8.5 KB/store at 272-B slots)
#endif
#if defined(MRINBOX_QSPI_READY)
extern mrinbox::DeviceInboxStore g_inbox_dm;
extern mrinbox::DeviceInboxStore g_inbox_ch;
#else
extern meshroute::FixedInboxStore<MR_RAM_INBOX_SLOTS> g_inbox_dm;
extern meshroute::FixedInboxStore<MR_RAM_INBOX_SLOTS> g_inbox_ch;
#endif

extern meshroute::Identity g_identity;   // seed -> Ed25519/X25519 + key_hash32

// ---- runtime / diagnostic / live-config state (fw_main.cpp block @142-171) ----
extern uint8_t  g_rxbuf[meshroute::protocol::max_payload_bytes_hard_cap + 32];
extern bool     g_radio_ok;
extern uint32_t g_rx_count;
extern uint32_t g_sleep_count;
extern bool     g_host_present;
extern bool     g_force_sleep;
extern double   g_freq_mhz;
extern int8_t   g_tx_power;
extern uint8_t  g_ble_mode;
extern uint8_t  g_ble_period_min;
extern uint32_t g_ble_pin;
extern int32_t  g_lat_e7;
extern int32_t  g_lon_e7;
extern uint8_t  g_persist_id, g_persist_epoch, g_persist_join;   // last DAD lease state written to NV (change-detect)
extern uint8_t  g_persist_team_local_id;                         // §mobile 6.4 team-DAD id
extern uint16_t g_ctr_lease;                                     // persisted channel-ctr lease (InternalFS self-heal Part 3)
extern bool     g_fs_reformatted;                               // mount_or_repair() reformatted a corrupt InternalFS this boot

// ---- inbox NDJSON scratch (shared: pull_inbox records AND live loop() push lines; single-threaded) ----
extern char s_inbox_jb[1700];

// ---- §remote-mgmt admin-ISSUE side (operator device): transient, wiped on lock/reboot ----
// Guarded to match the definitions (fw_main.cpp, #if MR_FEAT_REMOTE_MGMT). SHARED across the firmware_remote
// cluster (rcmd/unlock/lock) AND fw_main's mesh_service_once (opens sealed ACK/hint replies) — hence extern, not
// cluster-private. The definitions stay in fw_main.cpp; firmware_remote.cpp references them through here.
#if MR_FEAT_REMOTE_MGMT
extern meshroute::Identity g_admin_id;
extern bool                g_admin_unlocked;
extern uint32_t            g_admin_tx_ctr;
#endif

// ---- mesh loop task handle (nRF52 FreeRTOS only) ----
#if defined(NRF52_SERIES) || defined(ARDUINO_ARCH_NRF52) || defined(BOARD_XIAO_WIO_SX1262)
extern TaskHandle_t g_mesh_task;
#endif

// ---- shared fw_main helpers (defined in fw_main.cpp; referenced across a cluster boundary) ----
uint32_t loop_stack_free_bytes();   // nRF52 loop-task min free stack bytes (0 elsewhere) — used by dump_status AND firmware_remote's status TLV
void     fw_wdt_feed();             // kick the watchdog (wraps mrfault::fault_wdt_feed; device_fault.h defines ISR vectors so it can't be pulled into a 2nd TU) — used during firmware_remote's multi-second admin-key KDF
