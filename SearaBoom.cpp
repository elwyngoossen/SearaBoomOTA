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
#include "Audio.h"
#include "ESPAsyncWebServer.h"
#include "AsyncTCP.h"
#include "LittleFS.h"
#include <DNSServer.h>
#include <HTTPClient.h>
#include <Update.h>
#include <time.h>
#include "esp_task_wdt.h"

#define WDT_TIMEOUT_S  30   // Reboot if loop() stalls for 30 seconds

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
#define TOUCHTHRESS     3500
#define DEBOUNCE        250
#define WIFI_TIMEOUT    500
#define DEFAULT_VOLUME  8
#define DNS_INTERVAL    30

// -----------------------------------------------------------------------------
//  Stream URLs
// -----------------------------------------------------------------------------
#define URL1  "http://8396.brasilstream.com.br/stream?origem=SearaBoom"
#define URL2  "http://8404.brasilstream.com.br/stream?origem=SearaBoom"

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
  "<title>SearaBoom Configuração</title>"
  "<link rel=\"stylesheet\" href=\"styles.css\"></head><body><article>"
  "<form action=\"/\" method=\"POST\">"
  "<h1><img src=\"background.png\"></h1>"
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
Audio          audio;

String   ssid, password, url;
int      volume           = DEFAULT_VOLUME;
int      wifiConnectTimer = 0;
bool     volumeUpState    = false;
bool     volumeDownState  = false;
unsigned long lastDebounce  = 0;
unsigned long ledTimer      = 0;
bool     ledState           = false;
unsigned long lastWifiCheck   = 0;
unsigned long lastAudioActive = 0;

// -----------------------------------------------------------------------------
//  Forward Declarations
// -----------------------------------------------------------------------------
void blinkLed(uint8_t color, uint16_t interval);
String readFile(fs::FS& fs, const char* path);
void writeFile(fs::FS& fs, const char* path, const char* message);
void IRAM_ATTR onVolumeUp();
void IRAM_ATTR onVolumeDown();
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

// =============================================================================
//  Touch Interrupt Callbacks
// =============================================================================
void IRAM_ATTR onVolumeUp()   { volumeUpState   = true; }
void IRAM_ATTR onVolumeDown() { volumeDownState = true; }

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
    WiFi.softAPConfig(localIP, localGateway, subnet);
    WiFi.softAP("SearaBoomSetup", NULL);

    // Serve config page
    server.on("/", HTTP_GET, [html](AsyncWebServerRequest* request) {
        request->send(200, "text/html", html);
    });

    server.serveStatic("/", LittleFS, "/");

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
        request->redirect(localIPURL);
    });

    // DNS: catch all domains and point to us
    dnsServer.setTTL(3600);
    dnsServer.start(53, "*", localIP);

    server.begin();

    // Loop forever in setup mode (blue blink)
    while (true) {
        blinkLed(3, 500);
        dnsServer.processNextRequest();
        delay(DNS_INTERVAL);
    }
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
    Serial.printf("SSID: %s  URL: %s\n", ssid.c_str(), url.c_str());

    // Connect to WiFi (red blink while trying)
    WiFi.begin(ssid.c_str(), password.c_str());
    while (WiFi.status() != WL_CONNECTED) {
        blinkLed(1, 500);
        delay(50);
        wifiConnectTimer++;
        if (wifiConnectTimer > WIFI_TIMEOUT) {
            Serial.println("WiFi timeout -> setupMode");
            setupMode(); // Never returns
        }
    }
    Serial.println("WiFi connected");

    // Sync time via NTP (UTC-3 = Brazil)
    configTime(-3 * 3600, 0, "pool.ntp.org");

    // WiFi connected — check for OTA update before starting audio
    checkAndPerformOTA();
    Serial.println("OTA check done");

    // Init I2S audio
    audio.setBufsize(80000, 0); // 80KB RAM buffer (PSRAM disabled — Waveshare PSRAM crashes)
    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(volume);

    // Attach capacitive touch interrupts
    touchAttachInterrupt(TOUCHUP,   onVolumeUp,   TOUCHTHRESS);
    touchAttachInterrupt(TOUCHDOWN, onVolumeDown, TOUCHTHRESS);

    // Connect to the saved stream
    Serial.printf("Connecting to stream: %s\n", url.c_str());
    if (url == "URL1") {
        audio.connecttohost(URL1);
    } else {
        audio.connecttohost(URL2);
    }
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
    audio.loop();
    vTaskDelay(1);    // Required by audioI2S — lets RTOS run other tasks

    // Stream activity watchdog — reconnect if audio stops for 30s
    if (audio.isRunning()) {
        lastAudioActive = millis();
    } else if (WiFi.status() == WL_CONNECTED && millis() - lastAudioActive > 30000) {
        Serial.printf("Audio stalled for %lus — heap: %u — reconnecting stream\n",
                      (millis() - lastAudioActive) / 1000, ESP.getFreeHeap());
        const char* streamUrl = (url == "URL1") ? URL1 : URL2;
        Serial.printf("Connecting to: %s\n", streamUrl);
        bool ok = audio.connecttohost(streamUrl);
        Serial.printf("connecttohost returned: %s\n", ok ? "OK" : "FAILED");
        lastAudioActive = millis();
    }
    static uint32_t lastHeap = 0;
    if (millis() - lastHeap > 5000) {
        lastHeap = millis();
        Serial.printf("Free heap: %u\n", ESP.getFreeHeap());
    }

    // WiFi watchdog — check every 15s, reconnect if dropped
    if (millis() - lastWifiCheck > 15000) {
        lastWifiCheck = millis();
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi lost, reconnecting...");
            rgbLedWrite(RGB_BUILTIN, 255, 0, 0); // Red while reconnecting
            WiFi.disconnect();
            WiFi.begin(ssid.c_str(), password.c_str());
            unsigned long start = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
                delay(500);
            }
            if (WiFi.status() == WL_CONNECTED) {
                Serial.printf("WiFi reconnected (RSSI: %d dBm), restarting stream\n", WiFi.RSSI());
                const char* streamUrl = (url == "URL1") ? URL1 : URL2;
                bool ok = audio.connecttohost(streamUrl);
                Serial.printf("connecttohost returned: %s\n", ok ? "OK" : "FAILED");
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

    // Volume Up
    if (volumeUpState) {
        if ((millis() - lastDebounce) > DEBOUNCE) {
            if (volume < 21) {          // audioI2S max is 21
                audio.setVolume(++volume);
                lastDebounce = millis();
            }
        }
        if (!touchInterruptGetLastStatus(TOUCHUP))
            volumeUpState = false;
    }

    // Volume Down
    if (volumeDownState) {
        if ((millis() - lastDebounce) > DEBOUNCE) {
            if (volume > 1) {
                audio.setVolume(--volume);
                lastDebounce = millis();
            }
        }
        if (!touchInterruptGetLastStatus(TOUCHDOWN))
            volumeDownState = false;
    }
}

// =============================================================================
//  Audio library callbacks — print status to Serial for debugging
// =============================================================================
void audio_info(const char *info)        { Serial.printf("[audio] %s\n", info); }
void audio_error(const char *info)       { Serial.printf("[audio ERROR] %s\n", info); }

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
