// MeshRoute — lib/core/frame_trace.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Compact, DECODED one-line trace of a wire frame for on-device debugging (RX + TX). Decodes the cmd
// nibble (§10) + the addressing fields per frame type, so the console shows from/to/dst/ctr instead of a
// raw cmd number. Shared by fw_main (RX, after poll_rx) and device_radio (TX, at arm time) so both
// directions read the same. DEVICE-ONLY (uses Serial); native/sim builds skip the whole header.
#pragma once
#ifndef MESHROUTE_NS
#define MESHROUTE_NS meshroute   // Slice 5 faithful two-lib: gateway variant compiles with -DMESHROUTE_NS=meshroute_gw
#endif
#if defined(ARDUINO)
#include <Arduino.h>
#include <span>
#include "frame_codec.h"   // parse_rts/cts/data/ack/nack + the *_out structs

namespace MESHROUTE_NS {

// cmd nibble (high 4 bits of byte 0) -> short name. §10: BCN0 RTS1 CTS2 DATA3 ACK4 NACK5 Q6 H7 F8 J9.
inline const char* mr_cmd_name(uint8_t cmd) {
    switch (cmd) {
        case 0x0: return "BCN";  case 0x1: return "RTS";  case 0x2: return "CTS";
        case 0x3: return "DATA"; case 0x4: return "ACK";  case 0x5: return "NACK";
        case 0x6: return "Q";    case 0x7: return "H";    case 0x8: return "F";
        case 0x9: return "J";    case 0xA: return "M";    default:  return "?";
    }
}

inline bool g_mr_trace_on = false;  // §3: default OFF — `debug on` enables the decoded RX/TX trace for a session

// Hex-dump frame[off, off+len) to the console, capped at `cap` bytes (..  if truncated). Used by the CRYPTED
// region annotation so an operator can EYE-CONFIRM the encrypted span isn't their plaintext.
inline void mr_trace_hex(const uint8_t* b, size_t off, size_t len, size_t cap) {
    const size_t show = (len > cap) ? cap : len;
    for (size_t i = 0; i < show; ++i) { uint8_t x = b[off + i]; if (x < 0x10) Serial.print('0'); Serial.print(x, HEX); }
    if (len > cap) Serial.print(F(".."));
}

// One line:  «rx <NAME> from= to= dst= … len= sf= [snr= rssi=] t=…ms   (»tx for is_rx=false; no snr/rssi)
// `sf` = the radio SF the frame arrived/left on. Addressing fields are per-type (see §10 / the user's ask):
//   RTS  from=src to=next dst=dst sfi=<sf_index 0..3, 3=ANY>      CTS  from=tx_id to=rx_id dsf=<data sf>
//   DATA to=next dst=dst ctr=<ctr>                                ACK  to=<to> ctr=<ctr_lo>
//   NACK to=<to> rsn=<reason>                                     BCN/Q/H/F/J: name only
inline void mr_trace_frame(bool is_rx, const uint8_t* b, size_t n, int sf,
                           float snr, float rssi, uint32_t t_ms) {
    if (!g_mr_trace_on) return;                    // `debug off` silences the per-frame trace
    const uint8_t cmd = n ? static_cast<uint8_t>(b[0] >> 4) : 0xFFu;
    Serial.println(F("")); Serial.print(F(" t=")); Serial.print(t_ms); Serial.print(F(" ms "));
    Serial.print(is_rx ? F("«rx ") : F("»tx "));
    Serial.print(mr_cmd_name(cmd));
    const std::span<const uint8_t> f(b, n);
    switch (cmd) {
        case 0x0: if( auto b = parse_beacon(f)) {
            Serial.print(F(" from=")); Serial.print(b->src);
            Serial.print(F(" n=")); Serial.print(b->n_entries);
            Serial.print(F(" flags="));
            if(b->has_schedule) Serial.print(F(" has_schedule"));
            if(b->self_gateway) Serial.print(F(" self_gateway"));
            if(b->is_mobile) Serial.print(F(" is_mobile"));
            if(b->has_seen_bitmap) Serial.print(F(" has_seen_bitmap"));
            if(b->has_ext) Serial.print(F(" has_ext"));
        } break;
        case 0x1: if (auto r = parse_rts(f))  { Serial.print(F(" from=")); Serial.print(r->src);
                      Serial.print(F(" to="));  Serial.print(r->next); Serial.print(F(" dst=")); Serial.print(r->dst);
                      Serial.print(F(" ctr=")); Serial.print(r->ctr_lo);   // WHICH DM this RTS is for (disambiguates concurrent DMs)
                      Serial.print(F(" sfi=")); Serial.print(r->sf_index); } break;
        case 0x2: if (auto c = parse_cts(f))  { Serial.print(F(" from=")); Serial.print(c->tx_id);
                      Serial.print(F(" to="));  Serial.print(c->rx_id); Serial.print(F(" dsf=")); Serial.print(c->chosen_data_sf);
                      if (c->already_received) Serial.print(F(" RCVD")); } break;   // dedup CTS: "I already have this DM" -> sender skips DATA
        case 0x3: if (auto d = parse_data(f)) { Serial.print(F(" to=")); Serial.print(d->next);
                        Serial.print(F(" dst=")); Serial.print(d->dst); Serial.print(F(" ctr=")); Serial.print(d->ctr);
                        if(d->app) {
                            Serial.print(F(" app=")); Serial.print(d->type); 
                            switch(d->type) {
                                case 1: Serial.print(F("H_ANSWER"));break;
                                case 2: Serial.print(F("AUTHORITATIVE_H_ANSWER"));break;
                                case 3: Serial.print(F("E2E_ACK"));break;
                                case 4: Serial.print(F("DATA_TYPE_H_ANSWER_PUBKEY"));break;
                                case 5: Serial.print(F("DATA_TYPE_AUTHORITATIVE_H_ANSWER_PUBKEY"));break;
                                
                            }
                        }
                        Serial.print(F(" flag="));
                        if( d->flags & 0x80 ) Serial.print(F("app "));
                        if( d->flags & 0x40 ) Serial.print(F("cross_layer "));
                        if( d->flags & 0x20 ) Serial.print(F("crypted "));
                        if( d->flags & 0x10 ) Serial.print(F("e2e_ack_req "));
                        if( d->flags & 0x08 ) Serial.print(F("location "));
                        if( d->flags & 0x04 ) Serial.print(F("source_hash "));
                        if( d->flags & 0x02 ) Serial.print(F("dst_hash "));
                        if( d->flags & 0x01 ) Serial.print(F("priority "));

                      // E2E eye-confirm: for a CRYPTED DATA, annotate which on-wire bytes are encrypted. The header
                      // (above) + aad are CLEARTEXT; enc[a..b) is the sealed body (opaque hex); tag+seed are trailers.
                      if (d->crypted) { const auto cr = data_crypted_region(*d);
                          if (cr.valid && cr.tag_off + cr.tag_len <= n) {
                              Serial.print(F(" CRYPTED enc[")); Serial.print((unsigned)cr.ct_off); Serial.print(F(".."));
                              Serial.print((unsigned)(cr.ct_off + cr.ct_len)); Serial.print(F(")="));
                              mr_trace_hex(b, cr.ct_off, cr.ct_len, 24);                          // the ENCRYPTED body bytes
                              Serial.print(F(" aad=")); mr_trace_hex(b, cr.aad_off, cr.aad_len, 8);   // cleartext dst_hash (§1c: origin sealed)
                              Serial.print(F(" tag[16] seed[8]")); } } } break;
        case 0x4: if (auto a = parse_ack(f))  { Serial.print(F(" to=")); Serial.print(a->to);
                      Serial.print(F(" ctr=")); Serial.print(a->ctr_lo); } break;
        case 0x5: if (auto k = parse_nack(f)) { Serial.print(F(" to=")); Serial.print(k->to);
                      Serial.print(F(" ctr=")); Serial.print(k->ctr_lo);
                      Serial.print(F(" rsn=")); Serial.print(k->reason); } break;
        case 0xA: if (auto mm = parse_m(f))   { Serial.print(F(" leaf=")); Serial.print(mm->leaf_id);
                      Serial.print(F(" ch=")); Serial.print(mm->channel_id);
                      Serial.print(F(" id=")); Serial.print(mm->channel_msg_id, HEX); } break;   // lean channel-message frame
        case 0x6: if (auto q = parse_q(f))  { Serial.print(F(" leaf=")); Serial.print(q->leaf_id);
                      Serial.print(F(" src=")); Serial.print(q->src);
                      Serial.print(F(" dest=")); Serial.print(q->dest);
                      Serial.print(F(" op="));
                      switch (q->opcode) {
                          case 1: Serial.print(F("REQ_SYNC")); break;
                          case 3: Serial.print(F("CHANNEL_PULL"));
                                  if (q->channel_id_count > 0) { Serial.print(F(" ch_ids=")); Serial.print(q->channel_id_count); }
                                  break;
                          default: Serial.print(q->opcode); break;
                      }
                      if (q->mobile) Serial.print(F(" mobile"));
                  } break;
        case 0x7: if (auto h = parse_h(f))  { Serial.print(F(" leaf=")); Serial.print(h->leaf_id);
                      Serial.print(F(" origin=")); Serial.print(h->origin);
                      Serial.print(F(" hash=")); Serial.print(h->key_hash32, HEX);
                      Serial.print(F(" ttl=")); Serial.print(h->ttl);
                      if (h->hard)    Serial.print(F(" HARD"));
                      if (h->want_pubkey) Serial.print(F(" WANT_PUBKEY"));
                  } break;
        case 0x8: if (auto fr = parse_f(f)) { Serial.print(F(" leaf=")); Serial.print(fr->leaf_id);
                      Serial.print(F(" origin=")); Serial.print(fr->origin);
                      Serial.print(fr->is_reply ? F(" RREP") : F(" RREQ"));
                      Serial.print(F(" dst=")); Serial.print(fr->dst_id);
                      if (fr->is_reply) { Serial.print(F(" next_hop=")); Serial.print(fr->ttl_or_next_hop); }
                      else { Serial.print(F(" ttl=")); Serial.print(fr->ttl_or_next_hop); }
                      Serial.print(F(" hops=")); Serial.print(fr->hops);
                      Serial.print(F(" relay=")); Serial.print(fr->relay);
                  } break;
        case 0x9: if (auto j = parse_j(f))  { Serial.print(F(" leaf=")); Serial.print(j->leaf_id);
                      if (j->gateway_capable) Serial.print(F(" gw"));
                      if (j->is_mobile)        Serial.print(F(" mobile"));
                      switch (j->opcode) {
                          case 0: Serial.print(F(" DISCOVER hash=")); Serial.print(j->key_hash32, HEX); break;
                          case 1: Serial.print(F(" CLAIM proposed=")); Serial.print(j->proposed_node_id);
                                  Serial.print(F(" epoch=")); Serial.print(j->claim_epoch);
                                  Serial.print(F(" hash=")); Serial.print(j->key_hash32, HEX); break;
                          case 2: Serial.print(F(" DENY denied=")); Serial.print(j->denied_node_id);
                                  Serial.print(F(" reason=")); Serial.print(j->reason);
                                  Serial.print(F(" owner_hash=")); Serial.print(j->owner_key_hash32, HEX);
                                  Serial.print(F(" claimant_hash=")); Serial.print(j->claimant_key_hash32, HEX); break;
                          case 3: Serial.print(F(" OFFER responder=")); Serial.print(j->responder_node_id);
                                  Serial.print(F(" resp_hash=")); Serial.print(j->responder_key_hash32, HEX); break;
                          default: Serial.print(F(" j_op=")); Serial.print(j->opcode); break;
                      }
                  } break;
        default: break;                                   // unrecognized cmd — just the name + common fields
    }
    Serial.print(F(" len=")); Serial.print(static_cast<unsigned>(n));
    Serial.print(F(" sf="));  Serial.print(sf);
    if (is_rx) { Serial.print(F(" snr="));  Serial.print(snr, 1);
                 Serial.print(F(" rssi=")); Serial.print(rssi, 0); }
    Serial.flush();
    //Serial.println(F(""));
}

}  // namespace meshroute
#endif  // ARDUINO
