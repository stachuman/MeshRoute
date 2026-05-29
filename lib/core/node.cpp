// MeshRoute — lib/core/node.cpp  (S2 skeleton)
#include "node.h"

namespace meshroute {

// Caller-allocated timer ids (the Node owns this id namespace; see hal.h).
static constexpr uint32_t kTxTimerId = 1;

// Raw placeholder frame — NOT a real protocol frame. S2 proves the byte path;
// real frames come with the codec track (pack_beacon etc.).
static const uint8_t kPlaceholderFrame[] = { 'M', 'R', 'S', '2', 0x00, 0x01 };

Node::Node(Hal& hal, uint8_t node_id, uint32_t key_hash32, const char* name)
    : _hal(hal), _node_id(node_id), _key_hash32(key_hash32) {
    (void)name;  // sim-debug only; the node identifies by node_id / key_hash32
}

void Node::on_init(const NodeConfig& cfg) {
    (void)cfg;  // S2 skeleton: NodeConfig is wired for real at R1.
    // Stagger the boot TX by node id so two firmware nodes in one test do not
    // transmit simultaneously and self-collide. Deterministic (no RNG): node 0
    // TXes at 1 s, node 1 at 2 s, ... each well clear of the other's airtime.
    const uint32_t delay_ms = 1000u + static_cast<uint32_t>(_node_id) * 1000u;
    (void)_hal.after(delay_ms, kTxTimerId);
}

void Node::on_timer(uint32_t timer_id) {
    if (timer_id != kTxTimerId) return;
    TxParams p;  // all sentinels → use the radio defaults (RF plan)
    p.label = "MRS2";
    const TxResult r = _hal.tx(kPlaceholderFrame, sizeof(kPlaceholderFrame), p);
    EventField f[] = {
        { .key = "len",    .type = EventField::T::i64, .i = static_cast<int64_t>(sizeof(kPlaceholderFrame)) },
        { .key = "result", .type = EventField::T::i64, .i = static_cast<int64_t>(static_cast<int>(r)) },
    };
    _hal.emit("mr_node_tx", f, 2);
}

void Node::on_recv(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    (void)bytes;  // S2 skeleton: we don't parse — just prove delivery.
    EventField f[] = {
        { .key = "len", .type = EventField::T::i64, .i = static_cast<int64_t>(len) },
        { .key = "snr", .type = EventField::T::f64, .f = static_cast<double>(meta.snr_db) },
    };
    _hal.emit("mr_node_rx", f, 2);
}

// Remaining callbacks: no-ops until the behaviour track wires real logic (R1+).
void Node::on_radio_busy(const BusyInfo& info)     { (void)info; }
void Node::on_preamble_detected(uint64_t time_ms)  { (void)time_ms; }
void Node::on_command(const char* cmd, char* out_reply, size_t reply_cap) {
    (void)cmd;
    if (out_reply && reply_cap > 0) out_reply[0] = '\0';
}

}  // namespace meshroute
