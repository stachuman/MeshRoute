
#include "firmware_ui_team.h"
#include <cstdio>
#include <cstring>
int main(){
 mrui::TeamRow t{};t.last_heard_s=12;
 const char formatted[]={'W','o','l','f','g',char(0xbb),0};
 std::memcpy(t.label,formatted,sizeof formatted);
 char out[mrui::kTeamLineCap];mrui::ui_team_row(out,sizeof out,false,t,mrui::GeoFix{});
 std::printf("formatted=");for(unsigned char c:out){if(!c)break;std::printf("%02x",c);}std::puts("");
 std::snprintf(t.label,sizeof t.label,"%s","Wolfgangetta");
 mrui::ui_team_row(out,sizeof out,false,t,mrui::GeoFix{});
 std::printf("synthetic_overlong=%s\n",out);
 mrui::UiSnapshot a{},b{};a.team_shown=b.team_shown=1;
 std::memcpy(a.team[0].label,formatted,sizeof formatted);b=a;
 std::printf("equal_formatted=%d\n",int(mrui::ui_team_rows_equal(a,b)));
 b.team[0].label[10]='X'; // synthetic invisible-tail corruption, not a published W4a row
 std::printf("different_invisible_tail=%d\n",int(mrui::ui_team_rows_equal(a,b)));
}
