// Measurement of current copy paths only; no W4a formatter implementation.
#include "firmware_ui_model.h"
#include <cstring>
#include <cstdio>
int main() {
 using namespace mrui; unsigned checks=0;
 const char mark[]={'W','o','l','f','g',char(0xbb),0};
 for(unsigned b=0;b<256;b++){if(uint8_t(ui_display_byte(uint8_t(b))) != ((b>=32 && b<127)?b:'.'))return 1;checks++;}
 UiSnapshot s{};s.now_ms=1000;UiModel m;
 m.on_gesture(Gesture::long_arm,s);s.now_ms=4500;m.on_gesture(Gesture::long_fire,s);
 SendReq req{};if(!m.take_send_request(req))return 2;checks++;
 m.on_send_accepted(SendKind::emergency,5000);m.on_reply(mark,"OK",6000);
 if(m.emergency()!=Emergency::reply || std::memcmp(m.reply_who(),mark,sizeof mark))return 3;checks++;
 char frozen_who[kLabelCap+1];std::snprintf(frozen_who,sizeof frozen_who,"%s",m.reply_who());
 if(std::memcmp(frozen_who,mark,sizeof mark))return 4;checks++;
 s.member[0].key_hash32=0x00BEDEAD;s.member[0].id=221;std::memcpy(s.member[0].name,mark,sizeof mark);
 std::memcpy(s.team[0].label,mark,sizeof mark);UiSnapshot frozen=s;
 if(std::memcmp(frozen.team[0].label,mark,sizeof mark))return 5;checks++;
 const auto ident=invite_id_rows(frozen.member,1,0x00BEDEAD);
 if(std::memcmp(ident.name,mark,sizeof mark))return 6;checks++;
 char row[kInviteRowCap];ui_fmt_invite_row(row,sizeof row,'>',frozen.member[0]);
 if(std::memcmp(row+1,mark,6)||std::strlen(row)!=19)return 7;checks++;
 printf("%u checks PASS: existing sanitizer, real model reply copy, snprintf freeze spelling, snapshot copy, InviteIdRows, row precision; BB retained\n",checks);
}
