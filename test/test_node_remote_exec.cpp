// MeshRoute — 7b-1 two-endpoint radio tests, using the Slice 5 fixture where it already lives.
#include "doctest.h"
#include <cstddef>
#include <cstdint>
void radmin7_node_exchange(size_t output_bytes);
void radmin7_node_pressure(bool operator_waiter);
void radmin7_node_cross_layer(uint8_t depth);
TEST_CASE("§radmin-7/rx real flight executes fake once, sends multi-frame output and byte-identical replay") {
    radmin7_node_exchange(415);
}
TEST_CASE("§radmin-7/rx silent fake result airs only terminal zero and ACK retains its tombstone") {
    radmin7_node_exchange(0);
}
TEST_CASE("§radmin-7/rx oversized fake output airs retained prefix and output_truncated") {
    radmin7_node_exchange(1649);
}
TEST_CASE("§radmin-7/rx ACK frees pool under owner CONTROL pressure; next pass executes; full TX queue paces") {
    radmin7_node_pressure(false);
}
TEST_CASE("§radmin-7/rx ACK frees pool under operator GENERAL pressure without touching its body") {
    radmin7_node_pressure(true);
}
TEST_CASE("§radmin-7/rx every OUTPUT and TERMINAL airs on reversed depth-two path after no-gateway refusal") {
    radmin7_node_cross_layer(2);
}
TEST_CASE("§radmin-7/rx every OUTPUT and TERMINAL airs on reversed depth-three path at its own chunk cap") {
    radmin7_node_cross_layer(3);
}
TEST_CASE("§radmin-7/rx every OUTPUT and TERMINAL airs on reversed depth-four path at its own chunk cap") {
    radmin7_node_cross_layer(4);
}
