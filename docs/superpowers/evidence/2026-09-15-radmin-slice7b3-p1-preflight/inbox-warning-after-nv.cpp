#include <cstring>
#include <string>
#include <vector>
#include <iostream>
#define F(x) x
std::vector<std::string> events;
struct Print { void println(const char* s) { events.push_back(std::string("print:")+s+"\n"); } };
struct Inbox { const char* name; bool ok; bool wipe(){events.push_back(name);return ok;} };
Inbox g_inbox_dm{"wipe-dm",true},g_inbox_ch{"wipe-ch",true};
namespace mrnv { bool ok; bool factory_erase(){events.push_back("erase-nv");return ok;} }
void fw_reboot(){events.push_back("reboot");}
static void handle_factory_reset(const char* arg, size_t n, Print& out) {
    while (n && *arg == ' ') { ++arg; --n; }
    if (n == 7 && !strncmp(arg, "confirm", 7)) {
        out.println(F("> factory reset — erasing all NV, rebooting…"));
        // §5: drop the durable inbox RECORDS (their store's domain); factory_erase does the NV slots + the meta.
        // ⛔ [[B134]] QG blocker 3: BOTH results are now checked. `wipe()` used to return `void`, so this verb
        //    rebooted claiming a factory state while records stayed RECOVERABLE on flash — a data-retention lie in
        //    the worst direction, since a user told the history is gone will act as if it is. ⚠ Both stores are
        //    wiped BEFORE the `&&` short-circuits could skip one: a partial erase must still erase what it can.
        // ★ "MAY remain" is the RULED wording (2026-08-29) and the qualifier is load-bearing: every segment can
        //   erase cleanly and the METADATA save still fail, so a flat "messages remain" would be its own overclaim.
        const bool dm_ok = g_inbox_dm.wipe(), ch_ok = g_inbox_ch.wipe();
        if (!mrnv::factory_erase()) out.println(F("> factory_reset WARN: an NV slot did not erase (boot re-defaults it)"));
        if (!dm_ok || !ch_ok) out.println(F("> factory_reset WARN: inbox erase incomplete (messages may remain on flash)"));
        fw_reboot();
    } else {
        out.println(F("> factory_reset WIPES ALL flash (config + identity + peers + inbox) and reboots to factory. Type 'factory_reset confirm' to proceed."));
    }
}

int main(){
 Print out;unsigned cases=0;
 for(unsigned mask=0;mask<8;mask++){
  g_inbox_dm.ok=mask&1;g_inbox_ch.ok=mask&2;mrnv::ok=mask&4;events.clear();
  handle_factory_reset("confirm",7,out);
  std::vector<std::string> expected{
   "print:> factory reset — erasing all NV, rebooting…\n","wipe-dm","wipe-ch"};
  if(!g_inbox_dm.ok||!g_inbox_ch.ok) expected.push_back("print:> factory_reset WARN: inbox erase incomplete (messages may remain on flash)\n");
  expected.push_back("erase-nv");
  if(!mrnv::ok)expected.push_back("print:> factory_reset WARN: an NV slot did not erase (boot re-defaults it)\n");
  expected.push_back("reboot");
  if(events!=expected){std::cerr<<"ORDER/BYTES assertion failed, case "<<mask<<"\n";return 1;}
  cases++;
 }
 std::cout<<cases<<" factory-reset failure combinations: exact output/order PASS\n";
}
