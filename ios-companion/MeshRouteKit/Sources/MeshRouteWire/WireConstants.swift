// MeshRouteWire — WireConstants.swift
//
// The BLE link constants (the firmware↔app contract). The node exposes a Nordic UART
// Service-compatible `bleuart` GATT: an RX characteristic (app→node, write) and a TX
// characteristic (node→app, notify). See docs/superpowers/specs/2026-06-10-ble-companion-ota-inbox-design.md §A.2.

import Foundation

public enum WireConstants {
    // ---- Nordic UART Service (NUS) UUIDs — the bleuart the node advertises ----
    public static let nusServiceUUID            = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
    public static let nusRXCharacteristicUUID   = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E" // app → node (write)
    public static let nusTXCharacteristicUUID   = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E" // node → app (notify)

    // ---- payload limits (mirror lib/core/protocol_constants.h — keep in lockstep) ----
    /// TxItem.inner[] buffer size: lora_max_frame_bytes - data_hdr_len - data_inner_overhead.
    public static let maxPayloadBytesHardCap = 241
    /// A DM body must fit the inner buffer minus the [origin] prefix.
    public static let dmMaxBodyBytes = 239
    /// A channel post body cap (dv:989).
    public static let channelMaxBodyBytes = 200

    /// Frames are newline-delimited in BOTH directions over the NUS characteristics.
    public static let lineTerminator: Character = "\n"
}
