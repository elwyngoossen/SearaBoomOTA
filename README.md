# 📻 SearaBoom — "Fixed-Tuned" IP Radio by Rádio Seara

Firmware for a Wi-Fi internet radio built on the **Waveshare ESP32-S3-Zero**. Streams live AAC radio from two Brazilian FM stations, supports over-the-air (OTA) firmware updates via GitHub, and provides a captive-portal web interface for Wi-Fi configuration — all controlled with capacitive touch buttons.

---

## Features

- **Live internet radio streaming** — streams AAC audio over I2S to a DAC/amplifier
- **Two station presets** — Nova Russas FM 102.7 and Ibiapina FM 104.7
- **Device-tagged stream origin** — each device appends the last 4 characters of its MAC address to the stream URL (e.g. `searaboom - D770`) so each unit is identifiable on the server
- **Captive portal setup** — on first boot (or Wi-Fi failure), opens a Wi-Fi hotspot with a browser-based configuration page; scans nearby 2.4 GHz networks and lets you pick from a dropdown
- **Default test credentials** — on a fresh flash with no saved credentials, tries SSID `test` / password `test` before falling back to setup mode
- **OTA updates** — on every boot, checks GitHub for a newer firmware version and self-updates if one is found
- **Capacitive touch volume control** — touch-up and touch-down pads adjust volume (1–21)
- **Both-pad hold to reset Wi-Fi** — holding both touch pads for 15 seconds clears saved credentials and reboots into setup mode; works both during normal playback and during the Wi-Fi connection phase
- **Scheduled auto-reboot** — reboots at 04:00, 12:00, and 20:00 (UTC-3 / Brazil time) to keep the stream fresh
- **NTP time sync** — syncs to `pool.ntp.org` on boot (UTC-3)
- **RGB status LED** — WS2812 LED on GPIO 21 provides at-a-glance status (see table below)
- **Settings stored in LittleFS** — Wi-Fi credentials and station choice survive reboots
- **Hardware watchdog** — ESP32 task watchdog timer reboots the device if `loop()` stalls for more than 60 seconds
- **Wi-Fi watchdog** — checks connection every 15 seconds; reconnects automatically if dropped, reboots if reconnection fails
- **Stream activity watchdog** — if audio stops while Wi-Fi is up, automatically reconnects after 30 seconds of silence
- **256 KB audio buffer** — large HTTP read buffer to handle poor internet connections without dropouts

---

## Hardware

| Component | Detail |
|---|---|
| Microcontroller | Waveshare ESP32-S3-Zero |
| Wi-Fi band | 2.4 GHz only (802.11 b/g/n) |
| Audio output | I2S DAC/amplifier |
| I2S DOUT | GPIO 2 |
| I2S BCLK | GPIO 3 |
| I2S LRC | GPIO 4 |
| Volume Up touch | T7 |
| Volume Down touch | T6 |
| Status LED | WS2812 RGB on GPIO 21 |

---

## LED Status Reference

| Colour | Pattern | Meaning |
|---|---|---|
| ⬜ White | Solid | Booting / OTA update in progress |
| 🔴 Red | Blinking | Connecting to Wi-Fi |
| 🔵 Blue | Blinking | Setup mode (captive portal active) |
| 🟢 Green | Blinking | Playing — stream is running normally |
| 🟢 Green | Single flash | OTA update succeeded (reboots immediately after) |
| 🔴 Red | Solid (3 s) | OTA update failed |

---

## First-Time Setup

1. Power on the device.
2. On a fresh flash it will attempt to connect to SSID `test` / password `test`. If that network is not available it will blink **blue** and broadcast **`SearaBoomSetup`**.
3. Connect your phone or computer to `SearaBoomSetup` (no password required).
4. A browser should open automatically (captive portal). If not, navigate to **`http://4.3.2.1`**.
5. Choose your station, select your Wi-Fi network from the **dropdown** (guaranteed 2.4 GHz), enter the password, and tap **Salvar**.
6. The device saves the settings and reboots. It blinks **red** while connecting, then **green** when the stream starts.

> **Important:** Always pick your network from the dropdown — never type the SSID manually. The dropdown is built from the ESP32's own scan so every entry is guaranteed to be 2.4 GHz. Typing the name risks accidentally using the 5 GHz band name, which the ESP32-S3 cannot connect to.

> **Wi-Fi timeout:** The device allows 60 seconds to connect before falling back to setup mode.

---

## Resetting Wi-Fi Credentials

Hold **both touch pads simultaneously for 15 seconds**. The device will erase the saved SSID and password and reboot into setup mode (blue blink). This gesture works at any time — during playback or during the red-blink Wi-Fi connection phase.

---

## OTA Updates

The device checks for updates on every boot after connecting to Wi-Fi.

**How it works:**
1. Fetches `version.txt` from this repository.
2. Compares the remote version to the compiled `FIRMWARE_VERSION`.
3. If different, downloads `firmware.bin` from the matching GitHub Release and flashes it.
4. On success: green flash → automatic reboot into new firmware.
5. On failure: red LED for 3 seconds → continues with existing firmware.

**To publish an update:**
1. Bump the version string in `version.txt` on the `main` branch.
2. Create a new GitHub Release with a tag matching that version (e.g. `1.0.2`).
3. Attach the compiled `firmware.bin` to the release.

---

## PlatformIO Setup

### lib_deps

```ini
lib_deps =
    esp32async/AsyncTCP@^3.4.10
    https://github.com/pschatzmann/arduino-audio-tools.git
    https://github.com/pschatzmann/arduino-libhelix.git
    esp32async/ESPAsyncWebServer@^3.10.1
```

### Build Flags

```ini
build_flags =
    -DARDUINO_USB_MODE=1
    -DARDUINO_USB_CDC_ON_BOOT=1
    -UBOARD_HAS_PSRAM
    -DCONFIG_BT_ENABLED=0
    -DCONFIG_BLUEDROID_ENABLED=0
    -DCONFIG_NIMBLE_ENABLED=0
    -DCONFIG_LWIP_IPV6=0
    -DCONFIG_LIBC_NEWLIB_NANO_FORMAT=1
    -Os
```

### Custom Partition Table (`partitions.csv`)

```
# Name,   Type, SubType,  Offset,   Size,     Flags
nvs,      data, nvs,      0x9000,   0x5000,
otadata,  data, ota,      0xe000,   0x2000,
app0,     app,  ota_0,    0x10000,  0x1E0000,
app1,     app,  ota_1,    0x1F0000, 0x1E0000,
spiffs,   data, spiffs,   0x3D0000, 0x30000,
```

---

## Serial Debug Output

When connected to a serial monitor at **115200 baud**, the device prints every 5 seconds:

```
Free heap: 163808
WiFi RSSI: -57 dBm
Touch UP (T7): 17337  Touch DOWN (T6): 17361  Threshold: 21000
```

**RSSI guide:**

| RSSI | Signal quality |
|---|---|
| -50 dBm or better | Excellent |
| -60 to -70 dBm | Good |
| -70 to -80 dBm | Weak — may cause dropouts |
| Below -80 dBm | Very weak — expect issues |

---

## Configuration Reference

Key constants at the top of the sketch:

| Constant | Default | Description |
|---|---|---|
| `FIRMWARE_VERSION` | `"1.0.0"` | Current firmware version string |
| `WDT_TIMEOUT_S` | `60` s | Hardware watchdog timeout |
| `DEFAULT_VOLUME` | `8` | Volume on boot (range 1–21) |
| `TOUCHTHRESS` | `21000` | Capacitive touch sensitivity threshold |
| `DEBOUNCE` | `250` ms | Touch debounce interval |
| `WIFI_TIMEOUT` | `1200` | Loop iterations before setup mode (~60 s) |
| `URL1_BASE` | Nova Russas stream | Primary station base URL |
| `URL2_BASE` | Ibiapina stream | Secondary station base URL |

---

## License

MIT — see `LICENSE` for details.
