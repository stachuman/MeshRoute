/* Read-only host measurement of the installed panel font; no MeshRoute implementation. */
#include <stdio.h>
#include <string.h>
#include "u8g2.h"
extern uint8_t u8g2_font_decode_get_unsigned_bits(u8g2_font_decode_t *, uint8_t);
int main(void) {
 u8g2_t u={0}; u8g2_SetFont(&u,u8g2_font_6x10_tf);
 for(unsigned c=0;c<256;c++) {
  unsigned present=u8g2_IsGlyph(&u,c); int advance=u8g2_GetGlyphWidth(&u,c);
  printf("%02X present=%u advance=%d\n",c,present,advance);
 }
 int adv=u8g2_GetGlyphWidth(&u,0xBB); unsigned w=u.font_decode.glyph_width,h=u.font_decode.glyph_height;
 if(!u8g2_IsGlyph(&u,0xBB)||adv!=6||w*h>1024)return 1;
 printf("BB width=%u height=%u advance=%d xoffset=%d\n",w,h,adv,u.glyph_x_offset);
 char pix[1024];unsigned n=0, rounds=0;
 while(n<w*h) {
  unsigned a=u8g2_font_decode_get_unsigned_bits(&u.font_decode,u.font_info.bits_per_0);
  unsigned b=u8g2_font_decode_get_unsigned_bits(&u.font_decode,u.font_info.bits_per_1);
  do {
   for(unsigned j=0;j<a && n<w*h;j++)pix[n++]='.';
   for(unsigned j=0;j<b && n<w*h;j++)pix[n++]='#';
   if(++rounds>2048)return 2;
  } while(u8g2_font_decode_get_unsigned_bits(&u.font_decode,1));
 }
 for(unsigned y=0;y<h;y++)printf("%.*s\n",(int)w,pix+y*w);
 return 0;
}
