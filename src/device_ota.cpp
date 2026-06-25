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
    // Read raw POST body — avoid multipart parsing which is broken on this platform fork.
    // The HTML page below uses a simple form that posts the file as raw binary.
    size_t total = s_server.arg("plain").length();
    if (total == 0) {
        // Try reading the body from the raw request
        total = s_server.client().available();
        if (total == 0) {
            s_server.send(400, "text/plain", "Empty upload — select a firmware.bin file");
            mrcon.println(F("OTA: empty upload"));
            return;
        }
    }
    mrcon.printf("OTA: receiving %u bytes\n", (unsigned)total);
    if (!Update.begin(total, U_FLASH)) {
        mrcon.print("OTA: begin failed: "); Update.printError(mrcon);
        s_server.send(500, "text/plain", "OTA begin failed"); return;
    }
    // Read body in chunks
    WiFiClient& client = s_server.client();
    size_t written = 0;
    uint8_t buf[1024];
    size_t remaining = total;
    while (remaining > 0) {
        size_t chunk = remaining < sizeof(buf) ? remaining : sizeof(buf);
        size_t r = client.read(buf, chunk);
        if (r == 0) break;
        if (Update.write(buf, r) != r) {
            mrcon.print("OTA: write failed: "); Update.printError(mrcon);
            s_server.send(500, "text/plain", "OTA write failed"); Update.abort(); return;
        }
        written += r; remaining -= r;
    }
    mrcon.printf("OTA: %u bytes written\n", (unsigned)written);
    if (written > 0 && Update.end(true)) {
        mrcon.printf("OTA: %u bytes flashed — rebooting\n", (unsigned)written);
        s_server.send(200, "text/plain", "OK — rebooting now");
        mrcon.flush(); delay(500);
        if (s_pre_reboot_hook) s_pre_reboot_hook();   // mark the reset deliberate -> classifies as REBOOT, not UNEXPECTED
        ESP.restart();
    } else {
        mrcon.println(F("OTA: end failed or 0 bytes"));
        Update.abort();
        s_server.send(400, "text/plain", "Upload failed");
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
