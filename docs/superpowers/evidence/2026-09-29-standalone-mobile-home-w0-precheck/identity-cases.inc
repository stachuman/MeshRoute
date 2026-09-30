static void init_identity_fixture() {
    auto b=seed_id("Live Name",9,0x31); b.lat_e7=521234567; b.lon_e7=211234567;
    (void)mrnv::save_id(b); mrnv::IdBlob loaded{};
    if(!mrnv::load_id(loaded)) std::abort();
    meshroute::identity_from_seed(g_identity,loaded.seed);
    g_node.set_identity(10,g_identity.key_hash32); g_node.set_crypto_identity(g_identity.x_secret,g_identity.ed_pub);
    g_node.set_name(loaded.name,loaded.name_len);
    g_lat_e7=loaded.lat_e7; g_lon_e7=loaded.lon_e7;
    g_node.mutable_config().lat_e7=g_lat_e7; g_node.mutable_config().lon_e7=g_lon_e7;
    mrprobe_nv().writes=0; mrprobe_nv().reads=0;
}

// Source-extracted BLE branches, byte-identical bodies; transport callback stands in for BLE hardware.
static void ble_sink(const char* p,size_t n) { ble_capture(p,n); }
static size_t extracted_ble_identity(const char* line,size_t len) {
    using namespace meshroute::console; using mrfw::handle_cfg_set; using mrfw::make_cfg_extras;
    if (len == 6 && !strncmp(line, "whoami", 6)) {
        mrnv::IdBlob idb{}; mrnv::load_id(idb);              // the /mrid name (no RAM copy kept; whoami is rare)
        const size_t nl = (idb.name_len <= sizeof idb.name) ? idb.name_len : 0;
        const auto ds = g_node.duty_status();               // duty snapshot in the ready object (app shows it on connect)
        // ready carries the 64-hex pubkey + the duty snapshot -> ~280 B, over the 256-B `out`; stream via the big
        // scratch like status/cfg (return 0 = no buffered single-line ack).
        meshroute::console::MobileReadyFields mob{};   // §S1: mobile/team snapshot (all omit-when-inactive -> a static node's ready is byte-identical)
        const meshroute::NodeConfig& rc = g_node.config();
        mob.is_mobile  = rc.is_mobile;
        mob.registered = g_node.mobile_attached();   // ★ §MH-S4 §4.1/§7.1: CONFIRMED attachment only — `ready` is app-facing, so it must not report a transmitted-but-unconfirmed CLAIM as registered (the §S0-4 defect). `mobile_registered()` is the LINK-LAYER/provisional flag and is deliberately not used here.
        mob.home       = g_node.mobile_home_id();
        mob.local      = g_node.mobile_local_id();
        mob.home_layer = g_node.mobile_home_layer();
        mob.hosting    = g_node.mobile_reg_count();
        mob.team_id    = rc.team_id;
        mob.team_local = g_node.team_local_id();
        mob.team_ch_key = g_node.team_channel_key_present();   // §team-ch-key (T-K1b): the LOCK-STATE BOOLEAN so the app's indicator never calls `team exportkey` just to test for presence. ★ The KEY ITSELF must NEVER ride `ready` — it is unsolicited and fires on every connect.
        const size_t m = write_ready(s_inbox_jb, sizeof s_inbox_jb, g_node.node_id(), g_node.key_hash32(), g_node.config(),
                                     "existing", g_node.inbox().storage_epoch(), g_hal.now(), idb.name, nl,
                                     g_identity.ed_pub, ds.pct, ds.avail_ms, mob);   // §4: export pubkey for the QR `p`; §S1: mobile/team state
        if (m) ble_sink(s_inbox_jb, m);
        return 0;
    }
    if (len > 8 && !strncmp(line, "cfg set ", 8)) {
        handle_cfg_set(line + 8, mrcon);
        const size_t m = write_cfg(s_inbox_jb, sizeof s_inbox_jb, g_node.config(), make_cfg_extras());
        if (m) ble_sink(s_inbox_jb, m);
        return 0;
    }
    return 999;
}

uint8_t g_persist_team_local_id = 0;
int main() {
    const char* commands[]={"cfg set name New Name","cfg set lat 53.25","cfg set lon 22.5","regen"};
    const char* faults[]={"healthy","absent","bad_magic","bad_version","short","oversize","unreadable","write_failed"};
    for(auto command:commands) for(auto fault:faults) {
        init_identity_fixture(); auto& nv=mrprobe_nv(); auto* slot=nv.find("mr","id");
        if(!strcmp(fault,"absent")) slot->used=false;
        if(!strcmp(fault,"bad_magic")) slot->data[0]^=0x40;
        if(!strcmp(fault,"bad_version")) slot->data[4]^=0x40;
        if(!strcmp(fault,"short")) slot->len=50;
        if(!strcmp(fault,"oversize")) slot->len=81;
        if(!strcmp(fault,"unreadable")) nv.ns_present=false;
        if(!strcmp(fault,"write_failed")) nv.fail_write=true;
        CaptureSink out; bool routed_ok=mrfw::dispatch(command,strlen(command),out);
        nv.ns_present=true; mrnv::IdBlob saved{}; bool loaded=mrnv::load_id(saved);
        char name[33]={}; auto nn=g_node.effective_name(name,32);
        printf("CASE cmd=%s fault=%s routed=%d loaded=%d writes=%d saved_name_len=%u saved_lat=%d saved_lon=%d live_name_len=%u live_lat=%d live_lon=%d seed_eq_live=%d out=",command,fault,routed_ok,loaded,nv.writes,saved.name_len,saved.lat_e7,saved.lon_e7,nn,g_lat_e7,g_lon_e7,!memcmp(saved.seed,g_identity.seed,32));
        for(size_t i=0;i<out.n;++i) { printf("%02x",(unsigned char)out.buf[i]); } puts("");
    }
    const char* names[]={"ABCDEFGHIJKLMNOPQRSTUVWXYZ012345","ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456","ABCDEFGHIJKLMNOPQRSTUVWXYZ01234\xc5\x82"," Two words  ","\"Two words\""};
    for(auto name:names) { init_identity_fixture(); CaptureSink out; char line[160]; snprintf(line,sizeof line,"cfg set name %s",name); mrfw::dispatch(line,strlen(line),out); mrnv::IdBlob saved{}; mrnv::load_id(saved); printf("NAME typed=%zu saved=%u writes=%d hex=",strlen(name),saved.name_len,mrprobe_nv().writes); for(size_t i=0;i<saved.name_len;++i)printf("%02x",(unsigned char)saved.name[i]); printf(" out=%s",out.buf); }

    for(auto cmd:{"cfg set name ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456","cfg set name New Name","cfg set wibble 1"}) {
        init_identity_fixture(); mrprobe_nv().fail_write=true; ble_reset(); Serial.reset();
        size_t result=extracted_ble_identity(cmd,strlen(cmd)); mrcon.service();
        printf("BLE cmd=%s ret=%zu bytes=%zu text=%s\n",cmd,result,g_ble_n,g_ble); printf("USB text=%s\n",Serial.out);
    }
    for(auto fault:{"bad_magic","absent"}) {
        init_identity_fixture(); auto* slot=mrprobe_nv().find("mr","id");
        if(!strcmp(fault,"bad_magic")) { slot->data[0]^=0x40; memcpy(slot->data+40,"BAD NAME!",9); } else slot->used=false;
        ble_reset(); extracted_ble_identity("whoami",6); printf("READY fault=%s text=%s\n",fault,g_ble);
    }
    for(auto cmd:{"cfg set leaf_name ABCDEFGHIJKLMNOPQ", "create layer=1 freq=869.4625 bw=125 sf=8 sf_list=8 duty=10 name=ABCDEFGHIJKLMNOPQ"}) {
        init_identity_fixture(); CaptureSink out; mrfw::dispatch(cmd,strlen(cmd),out); mrnv::Blob b{};bool ok=mrnv::load(b);
        printf("LEAF cmd=%s load=%d n=%u text=",cmd,ok,b.leaf_name_len); for(size_t i=0;i<b.leaf_name_len;++i)putchar(b.leaf_name[i]);printf(" out=%s\n",out.buf);
    }
    printf("LAYOUT IdBlob=%zu align=%zu name=%zu seed=%zu lat=%zu lon=%zu\n",sizeof(mrnv::IdBlob),alignof(mrnv::IdBlob),offsetof(mrnv::IdBlob,name),offsetof(mrnv::IdBlob,seed),offsetof(mrnv::IdBlob,lat_e7),offsetof(mrnv::IdBlob,lon_e7));
    return 0;
}
