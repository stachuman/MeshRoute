// MeshRouteWire — LineAccumulator.swift
//
// BLE notifications arrive as arbitrary byte chunks: one notification may carry a partial
// line, several lines, or split a line across chunks. This reassembles a byte stream into
// complete '\n'-delimited lines (the same job as meshroute_client.py's _read_loop). Pure +
// testable; the CoreBluetooth transport feeds it didUpdateValue payloads.

import Foundation

public struct LineAccumulator {
    private var buffer: [UInt8] = []
    /// Guard against an unterminated flood pinning memory if a peer never sends '\n'.
    private let maxBufferedBytes: Int

    public init(maxBufferedBytes: Int = 4096) { self.maxBufferedBytes = maxBufferedBytes }

    /// Append a chunk; return every complete line it completed (UTF-8 decoded, lossy).
    public mutating func append(_ data: Data) -> [String] {
        var lines: [String] = []
        for byte in data {
            if byte == UInt8(ascii: "\n") {
                lines.append(decodeAndReset())
            } else if byte != UInt8(ascii: "\r") {
                buffer.append(byte)
                if buffer.count > maxBufferedBytes { buffer.removeAll(keepingCapacity: true) } // drop a runaway line
            }
        }
        return lines
    }

    private mutating func decodeAndReset() -> String {
        let s = String(decoding: buffer, as: UTF8.self)
        buffer.removeAll(keepingCapacity: true)
        return s
    }
}
