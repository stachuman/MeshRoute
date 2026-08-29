// MeshRoute — src/firmware_inbox.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The INBOX / companion-sync cluster extracted from fw_main.cpp (cleanup 2026-07-14, codebase-review triage
// "split firmware by responsibility"). The `pull_inbox`/`mark_read` verbs stream the persistent inbox as NDJSON to
// a transport sink (USB mrcon OR a BLE LineSink) — one handler serves both consoles. Unblocked by the
// inline device inbox-store headers ([[B260]]: `device_inbox_fs_{nrf52,esp32}.h`; the old twin is DELETED).
//
// The shared NDJSON scratch (s_inbox_jb) and the BLE LineSink callback (ble_sink) are NOT inbox-private — they are
// used by routes/status/cfg/live-push too, so they stay in fw_main/fw_context. This cluster is just the inbox verbs
// + their pull callback (internal). The verbs operate on g_node.inbox(), not the g_inbox_* stores directly.
//
// DEVICE-layer header.
#pragma once
#include <Arduino.h>   // Print

namespace mrfw {

// `pull_inbox <dm_since> <chan_since>` — stream the inbox delta (DM block, channel block, inbox_end) to `out`.
void handle_pull_inbox(const char* args, Print& out);

// `mark_read <dm|chan> <seq>` — advance the per-store read cursor.
void handle_mark_read(const char* args, Print& out);

// `del_msg <dm|chan> <seq>` — §3.5 durable single-record delete (a tombstone append; no rewrite, no segment erase).
// Acks `{"ack":"del_msg","kind":…,"seq":N,"result":"erased|not_found|io_error"}` — THREE outcomes, never a bool.
void handle_del_msg(const char* args, Print& out);

}  // namespace mrfw
