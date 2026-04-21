// =============================================================================
//  SearaBoom - Internet Radio Firmware
//  Version: 1.0.1
//  Target:  Waveshare ESP32-S3-Zero
//
//  SIZE REDUCTION MEASURES APPLIED:
//    1. Bluetooth disabled via build flags (see notes at bottom)
//    2. All Serial debug output removed (saves flash + RAM)
//    3. IPv6 disabled via build flags
//    4. Nano newlib format enabled via build flags
//    5. LTO (Link Time Optimization) enabled via build flags
//    6. HTML strings moved to PROGMEM (saves RAM)
//    7. String class replaced with const char* where possible
//
//  OTA UPDATE WORKFLOW:
//    - On every boot, after WiFi connects, the device fetches version.txt
//      from GitHub. If the version there is newer than FIRMWARE_VERSION,
//      it downloads the .bin from GitHub Releases and flashes itself.
//    - To push an update: bump version.txt on GitHub, create a new Release
//      with the matching tag, attach the .bin file.
//
//  ARDUINO IDE TOOLS SETTINGS (required):
//    Board:              Waveshare ESP32-S3-Zero
//    USB CDC On Boot:    Enabled
//    Partition Scheme:   Custom  (use partitions.csv in sketch folder)
//    PSRAM:              OPI PSRAM
//    Upload Speed:       921600
//
//  CUSTOM PARTITION TABLE (partitions.csv in sketch folder):
//    # Name,   Type, SubType,  Offset,   Size,    Flags
//    nvs,      data, nvs,      0x9000,   0x5000,
//    otadata,  data, ota,      0xe000,   0x2000,
//    app0,     app,  ota_0,    0x10000,  0x1E0000,
//    app1,     app,  ota_1,    0x1F0000, 0x1E0000,
//    spiffs,   data, spiffs,   0x3D0000, 0x30000,
//
//  BUILD FLAGS (platformio.ini or Arduino IDE boards.txt override):
//    -DCONFIG_BT_ENABLED=0
//    -DCONFIG_BLUEDROID_ENABLED=0
//    -DCONFIG_LWIP_IPV6=0
//    -Os
//    -flto
// =============================================================================

#include <Arduino.h>
// Waveshare ESP32-S3-Zero: WS2812 RGB LED on GPIO 21
// This framework uses neopixelWrite instead of rgbLedWrite
#include "esp32-hal-rgb-led.h"
#define rgbLedWrite neopixelWrite
#undef RGB_BUILTIN
#define RGB_BUILTIN 21
#include "WiFi.h"
#include "AudioTools.h"
#include "AudioTools/Communication/HTTP/URLStream.h"
#include "AudioTools/AudioCodecs/CodecAACHelix.h"
#include "ESPAsyncWebServer.h"
#include "AsyncTCP.h"
#include "LittleFS.h"
#include <DNSServer.h>
#include <HTTPClient.h>
#include <Update.h>
#include <time.h>
#include "esp_task_wdt.h"

#define WDT_TIMEOUT_S  60   // Reboot if loop() stalls for 60 seconds

// -----------------------------------------------------------------------------
//  Pin Definitions
// -----------------------------------------------------------------------------
#define I2S_DOUT        2
#define I2S_BCLK        3
#define I2S_LRC         4
#define TOUCHUP         T7
#define TOUCHDOWN       T6

// -----------------------------------------------------------------------------
//  Tunable Constants
// -----------------------------------------------------------------------------
#define TOUCHTHRESS     21000
#define DEBOUNCE        250
#define WIFI_TIMEOUT    1200
#define DEFAULT_VOLUME  8
#define DNS_INTERVAL    30

// -----------------------------------------------------------------------------
//  Stream URLs
// -----------------------------------------------------------------------------
#define URL1_BASE  "https://8396.brasilstream.com.br/stream?origem=searaboom"
#define URL2_BASE  "http://8404.brasilstream.com.br/stream?origem=SearaBoom"

// -----------------------------------------------------------------------------
//  OTA Configuration  ← EDIT THESE FOR YOUR GITHUB REPO
// -----------------------------------------------------------------------------
#define FIRMWARE_VERSION    "1.0.0"
#define OTA_VERSION_URL     "https://raw.githubusercontent.com/elwyngoossen/SearaBoomOTA/main/version.txt"
#define OTA_FIRMWARE_BASE   "https://github.com/elwyngoossen/SearaBoomOTA/releases/download/"

// -----------------------------------------------------------------------------
//  Captive Portal Network Config
// -----------------------------------------------------------------------------
IPAddress localIP(4, 3, 2, 1);
IPAddress localGateway(4, 3, 2, 1);
IPAddress subnet(255, 255, 255, 0);
const char* localIPURL = "http://4.3.2.1";

// -----------------------------------------------------------------------------
//  LittleFS File Paths
// -----------------------------------------------------------------------------
const char* ssidPath = "/ssid.txt";
const char* passPath = "/pass.txt";
const char* urlPath  = "/url.txt";

// -----------------------------------------------------------------------------
//  HTML stored in PROGMEM to save RAM
// -----------------------------------------------------------------------------
const char HTML1[] PROGMEM =
  "<!DOCTYPE html><html lang=\"en\"><head>"
  "<meta charset=\"UTF-8\">"
  "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
  "<title>SearaBoom</title>"
  "<style>"
  "*{box-sizing:border-box;margin:0;padding:0}"
  "body{min-height:100vh;background:url('data:image/jpeg;base64,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') center/cover no-repeat fixed;display:flex;align-items:center;justify-content:center;font-family:Arial,sans-serif}"
  "article{background:rgba(255,255,255,0.88);border-radius:12px;padding:28px 24px;width:92%;max-width:380px;box-shadow:0 4px 20px rgba(0,0,0,0.3)}"
  "h1{text-align:center;color:#1a1a1a;font-size:1.5em;margin-bottom:18px}"
  "fieldset{border:none;margin-bottom:14px}"
  "legend{font-weight:bold;color:#444;margin-bottom:8px;font-size:.9em;text-transform:uppercase}"
  "select,input{width:100%;padding:10px;border:1px solid #ccc;border-radius:6px;font-size:1em;margin-top:4px}"
  "label{display:block;margin-top:10px;color:#555;font-size:.9em}"
  "button{width:100%;padding:12px;background:#f5c400;color:#1a1a1a;border:none;border-radius:6px;font-size:1.1em;font-weight:bold;cursor:pointer;margin-top:18px}"
  "</style></head><body><article>"
  "<form action=\"/\" method=\"POST\">"
  "<h1>SearaBoom</h1>"
  "<fieldset><fieldset><legend>Estação</legend>"
  "<select name=\"url\" id=\"URL\">"
  "<option value=\"URL1\">Nova Russas (FM 102.7)</option>"
  "<option value=\"URL2\">Ibiapina (FM 104.7)</option>"
  "</select></fieldset>"
  "<fieldset><legend>Configurações do WiFi</legend>"
  "<label for=\"SSID\">Nome:</label>"
  "<select name=\"ssid\" id=\"SSID\">";

const char HTML2[] PROGMEM =
  "</select>"
  "<label for=\"PASS\">Senha:</label>"
  "<input name=\"pass\" id=\"PASS\" type=\"Text\" placeholder=\"Senha Do WiFi\">"
  "</fieldset>"
  "<button type=\"submit\">Salvar</button>"
  "</form></article></body></html>";

// -----------------------------------------------------------------------------
//  Global State
// -----------------------------------------------------------------------------
AsyncWebServer server(80);
DNSServer      dnsServer;
URLStream          urlStream(262144); // 256KB buffer
I2SStream          i2sStream;
VolumeStream       volStream(i2sStream);
AACDecoderHelix    aacDecoder;
EncodedAudioStream decoder(&volStream, &aacDecoder);
StreamCopy         copier(decoder, urlStream);

String   ssid, password, url;
String   streamURL1, streamURL2;
char     icyClientId[32]     = "";
int      volume           = DEFAULT_VOLUME;
int      wifiConnectTimer = 0;
unsigned long lastDebounce  = 0;
unsigned long ledTimer      = 0;
bool     ledState           = false;
unsigned long lastWifiCheck   = 0;
unsigned long lastAudioActive = 0;
unsigned long bothTouchStart  = 0;

// -----------------------------------------------------------------------------
//  Forward Declarations
// -----------------------------------------------------------------------------
void blinkLed(uint8_t color, uint16_t interval);
String readFile(fs::FS& fs, const char* path);
void writeFile(fs::FS& fs, const char* path, const char* message);
void checkAndPerformOTA();
void setupMode();

// =============================================================================
//  LED Helper
// =============================================================================
// color: 1=red  2=green  3=blue
void blinkLed(uint8_t color, uint16_t interval) {
    if (millis() - ledTimer < interval) return;
    ledTimer = millis();
    ledState = !ledState;
    if (ledState) {
        switch (color) {
            case 1: rgbLedWrite(RGB_BUILTIN, 255, 0,   0);   break;
            case 2: rgbLedWrite(RGB_BUILTIN, 0,   255, 0);   break;
            case 3: rgbLedWrite(RGB_BUILTIN, 0,   0,   255); break;
        }
    } else {
        rgbLedWrite(RGB_BUILTIN, 0, 0, 0);
    }
}

// =============================================================================
//  LittleFS Helpers
// =============================================================================
String readFile(fs::FS& fs, const char* path) {
    File file = fs.open(path);
    if (!file || file.isDirectory()) return "";
    char buf[128] = {0};
    uint8_t i = 0;
    while (file.available() && i < sizeof(buf) - 1)
        buf[i++] = file.read();
    file.close();
    return String(buf);
}

void writeFile(fs::FS& fs, const char* path, const char* message) {
    File file = fs.open(path, FILE_WRITE);
    if (!file) return;
    file.print(message);
    file.close();
}

// Touch callbacks not used (polling instead)

// =============================================================================
//  OTA Update
//  Checks GitHub version.txt; if newer, downloads and flashes the binary.
//  Uses setInsecure() to avoid embedding a root CA certificate.
//  For production, replace with a pinned certificate for security.
// =============================================================================
void checkAndPerformOTA() {
    // --- Step 1: fetch version.txt ---
    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure();  // Accept any cert (fine for version check)

    http.begin(client, OTA_VERSION_URL);
    http.setTimeout(8000);
    int code = http.GET();

    if (code != HTTP_CODE_OK) {
        http.end();
        return;
    }

    String remoteVersion = http.getString();
    remoteVersion.trim();
    http.end();

    // --- Step 2: compare versions ---
    if (remoteVersion == FIRMWARE_VERSION) {
        return; // Already up to date
    }

    // --- Step 3: signal update starting (white LED) ---
    rgbLedWrite(RGB_BUILTIN, 128, 128, 128);

    // --- Step 4: download and flash ---
    String firmwareUrl = String(OTA_FIRMWARE_BASE) + remoteVersion + "/firmware.bin";

    WiFiClientSecure dlClient;
    dlClient.setInsecure();

    // GitHub Releases redirect once; follow it manually
    http.begin(dlClient, firmwareUrl);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(60000);

    code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        rgbLedWrite(RGB_BUILTIN, 255, 0, 0); // Red = failed
        delay(3000);
        return;
    }

    int contentLength = http.getSize();
    if (contentLength <= 0) {
        http.end();
        return;
    }

    if (!Update.begin(contentLength)) {
        http.end();
        return;
    }

    WiFiClient* stream = http.getStreamPtr();
    size_t written = Update.writeStream(*stream);
    http.end();

    if (written != (size_t)contentLength || !Update.end() || !Update.isFinished()) {
        Update.abort();
        rgbLedWrite(RGB_BUILTIN, 255, 0, 0); // Red = failed
        delay(3000);
        return;
    }

    // Success — green flash then reboot
    rgbLedWrite(RGB_BUILTIN, 0, 255, 0);
    delay(1500);
    ESP.restart();
}

// =============================================================================
//  WiFi Setup Mode (Captive Portal)
// =============================================================================
void setupMode() {
    rgbLedWrite(RGB_BUILTIN, 128, 128, 128);

    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    WiFi.mode(WIFI_STA);
    delay(100);

    // Scan for nearby networks
    int n = WiFi.scanNetworks(false);

    // Build the full HTML page
    String html = FPSTR(HTML1);
    if (n < 1) {
        html += F("<option value=\"\">Nenhuma Rede Encontrada</option>");
    } else {
        for (int i = 0; i < n; i++) {
            html += F("<option value=\"");
            html += WiFi.SSID(i);
            html += F("\">");
            html += WiFi.SSID(i);
            if (WiFi.RSSI(i) <= -85)
                html += F(" (sinal fraco)");
            html += F("</option>");
        }
    }
    html += FPSTR(HTML2);
    WiFi.scanDelete();

    // Start soft AP
    WiFi.mode(WIFI_AP);
    delay(100);
    WiFi.softAPConfig(localIP, localGateway, subnet);
    WiFi.softAP("SearaBoomSetup", NULL);
    delay(500); // let AP come up before starting server

    // Serve config page
    server.on("/", HTTP_GET, [html](AsyncWebServerRequest* request) {
        request->send(200, "text/html", html);
    });

    // Handle form POST
    server.on("/", HTTP_POST, [](AsyncWebServerRequest* request) {
        int params = request->params();
        for (int i = 0; i < params; i++) {
            const AsyncWebParameter* p = request->getParam(i);
            if (!p->isPost()) continue;
            if (p->name() == "ssid")
                writeFile(LittleFS, ssidPath, p->value().c_str());
            else if (p->name() == "pass")
                writeFile(LittleFS, passPath, p->value().c_str());
            else if (p->name() == "url")
                writeFile(LittleFS, urlPath, p->value().c_str());
        }
        request->send(200, "text/plain", "Salvo. Reiniciando...");
        delay(2000);
        ESP.restart();
    });

    // Redirect all unknown requests to portal (captive portal behaviour)
    server.onNotFound([](AsyncWebServerRequest* request) {
        String path = request->url();
        // Only serve known static files to avoid LittleFS VFS errors on probes like /generate_204
        if (path == "/styles.css" || path == "/background.jpg") {
            request->send(LittleFS, path);
        } else {
            request->redirect(localIPURL);
        }
    });

    // DNS: catch all domains and point to us
    dnsServer.setTTL(3600);
    dnsServer.start(53, "*", localIP);

    server.begin();

    // Loop in setup mode (blue blink) — exit after 60s if no credentials submitted
    unsigned long setupStart = millis();
    while (millis() - setupStart < 60000) {
        esp_task_wdt_reset();
        blinkLed(3, 500);
        dnsServer.processNextRequest();
        delay(DNS_INTERVAL);
    }

    // Timeout — stop portal and reboot to retry stored credentials
    Serial.println("Setup timeout — rebooting");
    server.end();
    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    delay(500);
    ESP.restart();
}

// =============================================================================
//  setup()
// =============================================================================
void setup() {
    Serial.begin(115200);
    Serial.println("Boot");
    rgbLedWrite(RGB_BUILTIN, 128, 128, 128); // White while booting

    if (!LittleFS.begin(true)) { Serial.println("LittleFS failed"); return; }

    ssid     = readFile(LittleFS, ssidPath);
    password = readFile(LittleFS, passPath);
    url      = readFile(LittleFS, urlPath);
    if (ssid.isEmpty()) { ssid = "test"; password = "test"; }
    Serial.printf("SSID: %s  URL: %s\n", ssid.c_str(), url.c_str());

    // Connect to WiFi (red blink while trying)
    WiFi.setSleep(false); // Disable power save for reliable connection
    WiFi.begin(ssid.c_str(), password.c_str());
    while (WiFi.status() != WL_CONNECTED) {
        blinkLed(1, 500);
        delay(50);
        wifiConnectTimer++;
        if (wifiConnectTimer > WIFI_TIMEOUT) {
            Serial.println("WiFi timeout -> setupMode");
            setupMode(); // Never returns
        }
        // Both pads held during red-blink -> wipe credentials and go to setup mode
        if (touchRead(TOUCHUP) > TOUCHTHRESS && touchRead(TOUCHDOWN) > TOUCHTHRESS) {
            Serial.println("Both pads held during connect — clearing credentials");
            writeFile(LittleFS, ssidPath, "");
            writeFile(LittleFS, passPath, "");
            setupMode(); // Never returns
        }
    }
    Serial.println("WiFi connected");
    // Set unique Icy-ClientId from device MAC address
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    mac.toCharArray(icyClientId, sizeof(icyClientId));
    Serial.printf("Icy-ClientId: %s\n", icyClientId);
    String macSuffix = mac.substring(mac.length() - 4);
    macSuffix.toUpperCase();
    streamURL1 = String(URL1_BASE) + "%20-%20" + macSuffix;
    streamURL2 = String(URL2_BASE) + "%20-%20" + macSuffix;
    Serial.printf("Stream URLs: %s | %s\n", streamURL1.c_str(), streamURL2.c_str());

    // Sync time via NTP (UTC-3 = Brazil)
    configTime(-3 * 3600, 0, "pool.ntp.org");

    // WiFi connected — check for OTA update before starting audio
    checkAndPerformOTA();
    Serial.println("OTA check done");

    // Init I2S audio
    auto i2sCfg = i2sStream.defaultConfig(TX_MODE);
    i2sCfg.pin_bck  = I2S_BCLK;
    i2sCfg.pin_ws   = I2S_LRC;
    i2sCfg.pin_data = I2S_DOUT;
    i2sStream.begin(i2sCfg);

    volStream.begin();
    volStream.setVolume(volume / 21.0f);

    decoder.begin();

    // No interrupt setup needed — touch is polled in loop()

    // Connect to the saved stream
    urlStream.addRequestHeader("User-Agent", "VLC/3.0.20");
    const char* bootUrl = (url == "URL1") ? streamURL1.c_str() : streamURL2.c_str();
    Serial.printf("Connecting to stream: %s\n", bootUrl);
    urlStream.begin(bootUrl);
    lastAudioActive = millis();

    // Hardware watchdog — reboots if loop() stalls for WDT_TIMEOUT_S seconds
    esp_task_wdt_init(WDT_TIMEOUT_S, true);
    esp_task_wdt_add(NULL);

    Serial.println("setup() done");
}

// =============================================================================
//  loop()
// =============================================================================
void loop() {
    esp_task_wdt_reset();             // Feed hardware watchdog
    blinkLed(2, 500); // Blink green = playing

    // Stream activity watchdog — reconnect if audio stops for 30s
    if (copier.copy() > 0) {
        lastAudioActive = millis();
    } else if (WiFi.status() == WL_CONNECTED && millis() - lastAudioActive > 30000) {
        Serial.printf("Audio stalled for %lus — heap: %u — reconnecting stream\n",
                      (millis() - lastAudioActive) / 1000, ESP.getFreeHeap());
        const char* streamUrl = (url == "URL1") ? streamURL1.c_str() : streamURL2.c_str();
        Serial.printf("Connecting to: %s\n", streamUrl);
        urlStream.begin(streamUrl);
        lastAudioActive = millis();
    }
    static uint32_t lastHeap = 0;
    if (millis() - lastHeap > 5000) {
        lastHeap = millis();
        Serial.printf("Free heap: %u\n", ESP.getFreeHeap());
        Serial.printf("WiFi RSSI: %d dBm\n", WiFi.RSSI());
        Serial.printf("Touch UP (T7): %u  Touch DOWN (T6): %u  Threshold: %u\n",
                      touchRead(TOUCHUP), touchRead(TOUCHDOWN), TOUCHTHRESS);
    }

    // WiFi watchdog — check every 15s, reconnect if dropped
    if (millis() - lastWifiCheck > 15000) {
        lastWifiCheck = millis();
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi lost, reconnecting...");
            rgbLedWrite(RGB_BUILTIN, 255, 0, 0); // Red while reconnecting
            WiFi.disconnect();
            WiFi.setSleep(false);
            WiFi.begin(ssid.c_str(), password.c_str());
            unsigned long start = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
                delay(500);
            }
            if (WiFi.status() == WL_CONNECTED) {
                Serial.printf("WiFi reconnected (RSSI: %d dBm), restarting stream\n", WiFi.RSSI());
                const char* streamUrl = (url == "URL1") ? streamURL1.c_str() : streamURL2.c_str();
                urlStream.begin(streamUrl);
            } else {
                Serial.println("WiFi reconnect failed, rebooting");
                ESP.restart();
            }
        }
    }

    // Scheduled reboot at 04:00, 12:00 and 20:00
    static int lastRebootHour = -1;
    struct tm t;
    if (getLocalTime(&t, 0)) {
        if ((t.tm_hour == 4 || t.tm_hour == 12 || t.tm_hour == 20) &&
             t.tm_min == 0 && t.tm_hour != lastRebootHour) {
            lastRebootHour = t.tm_hour;
            ESP.restart();
        }
    }

    // Hold both pads 15s to force reboot
    bool bothTouched = (touchRead(TOUCHUP) > TOUCHTHRESS) && (touchRead(TOUCHDOWN) > TOUCHTHRESS);
    if (bothTouched) {
        if (bothTouchStart == 0) bothTouchStart = millis();
        if (millis() - bothTouchStart > 15000) {
            Serial.println("Both pads held — clearing WiFi credentials and rebooting");
            writeFile(LittleFS, ssidPath, "");
            writeFile(LittleFS, passPath, "");
            ESP.restart();
        }
    } else {
        bothTouchStart = 0;
    }

    // Volume Up — require 2 consecutive reads above threshold to filter noise
    static uint8_t upCount = 0;
    if (touchRead(TOUCHUP) > TOUCHTHRESS) { upCount++; } else { upCount = 0; }
    if (upCount >= 5) {
        if ((millis() - lastDebounce) > DEBOUNCE) {
            if (volume < 21) {
                ++volume;
                volStream.setVolume(volume / 21.0f);
                Serial.printf("Volume: %d\n", volume);
                lastDebounce = millis();
            }
        }
    }

    // Volume Down — require 2 consecutive reads above threshold to filter noise
    static uint8_t downCount = 0;
    if (touchRead(TOUCHDOWN) > TOUCHTHRESS) { downCount++; } else { downCount = 0; }
    if (downCount >= 5) {
        if ((millis() - lastDebounce) > DEBOUNCE) {
            if (volume > 1) {
                --volume;
                volStream.setVolume(volume / 21.0f);
                Serial.printf("Volume: %d  (DOWN raw: %u)\n", volume, touchRead(TOUCHDOWN));
                lastDebounce = millis();
            }
        }
    }
}

// =============================================================================
//  NOTES FOR SIZE REDUCTION IN ARDUINO IDE
//
//  The following steps must be done OUTSIDE the .ino to take full effect.
//  They are not possible from within the sketch itself.
//
//  1. PARTITION TABLE
//     Create a file called "partitions.csv" in the same folder as this .cpp:
//
//     # Name,   Type, SubType, Offset,   Size,     Flags
//     nvs,      data, nvs,     0x9000,   0x5000,
//     otadata,  data, ota,     0xe000,   0x2000,
//     app0,     app,  ota_0,   0x10000,  0x1E0000,
//     app1,     app,  ota_1,   0x1F0000, 0x1E0000,
//     spiffs,   data, spiffs,  0x3D0000, 0x30000,
//
//     Then in Arduino IDE: Tools → Partition Scheme → "Custom"
//     (This gives each OTA slot 1,920 KB = 1.875 MB)
//
//  2. DISABLE BLUETOOTH (biggest saving ~200-300 KB)
//     In Arduino IDE this requires editing the board's platform.txt.
//     Easier path: use PlatformIO and add to platformio.ini:
//       build_flags =
//         -DCONFIG_BT_ENABLED=0
//         -DCONFIG_BLUEDROID_ENABLED=0
//         -DCONFIG_NIMBLE_ENABLED=0
//
//  3. DISABLE IPv6 (~20-30 KB saving)
//     Add to build_flags:
//       -DCONFIG_LWIP_IPV6=0
//
//  4. ENABLE LINK TIME OPTIMIZATION (~10-20% saving)
//     Add to build_flags:
//       -flto
//     Or in Arduino IDE: the LTO option appears in Tools menu for some
//     board packages — enable it if present.
//
//  5. NANO NEWLIB FORMAT (~25-50 KB saving)
//     Add to build_flags:
//       -DCONFIG_LIBC_NEWLIB_NANO_FORMAT=1
//
//  NOTE: Bluetooth cannot be disabled from within Arduino IDE's GUI alone
//  for the ESP32-S3. If you are unable to use PlatformIO, the most impactful
//  alternative is to upgrade to the 8MB flash version of the ESP32-S3-Zero,
//  which removes the size constraint entirely.
// =============================================================================
