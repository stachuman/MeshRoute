// MeshRoute — src/device_ota.cpp
// ESP32-S3 WiFi OTA (SoftAP + WebServer + Update) — board-agnostic; works on any ESP32 with WiFi
// (Heltec V3 + XIAO ESP32-S3). Compiled in via build_src_filter on the ESP32 envs. The guard matches
// fw_main's mrota:: call-site condition so every ESP32 build that calls these also defines them.
#if defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(BOARD_HELTEC_V3)

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include "device_ota.h"
#include "console_sink.h"   // `mrcon` guarded sink

namespace mrota {

static WebServer s_server(80);
static bool       s_active = false;
static void     (*s_pre_reboot_hook)() = nullptr;   // fw_main marks the upcoming ESP.restart() as a deliberate reset (v2 fault log)

void set_pre_reboot_hook(void (*fn)()) { s_pre_reboot_hook = fn; }

static const char kOtaPage[] PROGMEM = R"raw(
<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>MeshRoute OTA</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>body{font-family:system-ui,sans-serif;max-width:420px;margin:40px auto;padding:0 16px}
h2{color:#333}input[type=file]{margin:12px 0}button{padding:8px 20px;cursor:pointer}
#msg{margin-top:12px}#msg.ok{color:green}#msg.err{color:#c00}</style></head>
<body>
<h2>MeshRoute Firmware Update</h2>
<p>Select <b>firmware.bin</b> and click Upload. Node reboots after flash.</p>
<input type="file" id="fw" accept=".bin"><br>
<button onclick="upload()">Upload</button>
<div id="msg"></div>
<script>
async function upload(){
  const f=document.getElementById('fw').files[0];
  if(!f){msg('Select a file first','err');return}
  msg('Uploading '+f.name+' ('+(f.size/1024).toFixed(0)+' KB)...','');
  try{
    const r=await fetch('/update',{method:'POST',body:f});
    const t=await r.text();
    msg(t,r.ok?'ok':'err');
  }catch(e){msg('Error: '+e.message,'err')}
}
function msg(t,c){const m=document.getElementById('msg');m.textContent=t;m.className=c||''}
</script>
</body></html>)raw";

static const char kOtaTail[] PROGMEM = R"raw(</div></body></html>)raw";

static void handle_update() {
    // The browser POSTs the raw firmware.bin as the request BODY (no multipart — broken on this platform fork). The
    // WebServer has already received the FULL Content-Length body and buffered it into arg("plain"), so its length is
    // the COMPLETE image — the authoritative size. (The old code took the size from arg("plain")/client().available()
    // then re-read from an already-drained client(), treating a 0-length read as EOF and committing whatever partial
    // amount it got via Update.end(true) — H2: a short/stalled upload flashed a TRUNCATED image = brick.)
    const String& body = s_server.arg("plain");
    const size_t total = body.length();
    if (total == 0) {
        s_server.send(400, "text/plain", "Empty upload — select a firmware.bin file");
        mrcon.println(F("OTA: empty upload"));
        return;
    }
    mrcon.printf("OTA: received %u bytes (complete body)\n", (unsigned)total);
    if (!Update.begin(total, U_FLASH)) {
        mrcon.print("OTA: begin failed: "); Update.printError(mrcon);
        s_server.send(500, "text/plain", "OTA begin failed"); return;
    }
    // Write the ENTIRE received image in one pass (it's already fully buffered — no chunked re-read, so no
    // stall-as-EOF truncation). Then finalize ONLY if the image is COMPLETE: Update.end(true) internally verifies
    // written == begin() size (isFinished), and we also require w == total. A short/torn body -> abort, NEVER flash.
    const size_t w = Update.write(const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(body.c_str())), total);
    if (w == total && Update.end(true)) {   // end(true) commits ONLY a complete image; else it errors + we abort
        mrcon.printf("OTA: %u bytes flashed (complete) — rebooting\n", (unsigned)total);
        s_server.send(200, "text/plain", "OK — rebooting now");
        mrcon.flush(); delay(500);
        if (s_pre_reboot_hook) s_pre_reboot_hook();   // mark the reset deliberate -> classifies as REBOOT, not UNEXPECTED
        ESP.restart();
    } else {
        mrcon.printf("OTA: INCOMPLETE (%u/%u) or verify-failed — NOT flashed\n", (unsigned)w, (unsigned)total);
        Update.printError(mrcon);
        Update.abort();   // discard the partial write; the running image is untouched
        s_server.send(400, "text/plain", "Incomplete/failed upload — not flashed (image unchanged)");
    }
}

static void handle_root() {
    s_server.send(200, "text/html", FPSTR(kOtaPage));
}

bool ota_start() {
    if (s_active) return true;
    if (!WiFi.softAP("MeshRoute-OTA")) {
        mrcon.println(F("OTA: SoftAP start FAILED"));
        return false;
    }
    mrcon.print(F("OTA: SoftAP 'MeshRoute-OTA' IP="));
    mrcon.println(WiFi.softAPIP());
    s_server.on("/",       HTTP_GET,  handle_root);
    s_server.on("/update", HTTP_POST, handle_update);
    s_server.begin();
    s_active = true;
    mrcon.println(F("OTA: browse to the IP above, upload firmware.bin"));
    return true;
}

void ota_stop() {
    if (!s_active) return;
    s_server.stop();
    WiFi.softAPdisconnect(true);
    s_active = false;
    mrcon.println(F("OTA: stopped"));
}

void ota_loop() {
    if (s_active) s_server.handleClient();
}

bool ota_active() { return s_active; }

}  // namespace mrota
#endif  // ESP32 (Heltec V3 / XIAO ESP32-S3)
