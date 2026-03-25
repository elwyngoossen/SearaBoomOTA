# 📻 SearaBoom — Internet Radio Firmware

Firmware for a Wi-Fi internet radio built on the **Waveshare ESP32-S3-Zero**. Streams live radio from two Brazilian FM stations, supports over-the-air (OTA) firmware updates via GitHub, and provides a captive-portal web interface for Wi-Fi configuration — all controlled with capacitive touch buttons.

---

## Features

- **Live internet radio streaming** — streams audio over I2S to a DAC/amplifier
- **Two station presets** — Nova Russas FM 102.7 and Ibiapina FM 104.7
- **Captive portal setup** — on first boot (or Wi-Fi failure), opens a Wi-Fi hotspot with a browser-based configuration page; scans nearby networks and lets you pick yours
- **OTA updates** — on every boot, checks GitHub for a newer firmware version and self-updates if one is found
- **Capacitive touch volume control** — touch-up and touch-down pads adjust volume (1–21)
- **Scheduled auto-reboot** — reboots at 04:00, 12:00, and 20:00 (UTC-3 / Brazil time) to keep the stream fresh
- **NTP time sync** — syncs to `pool.ntp.org` on boot (UTC-3)
- **RGB status LED** — WS2812 LED on GPIO 21 provides at-a-glance status (see table below)
- **Settings stored in LittleFS** — Wi-Fi credentials and station choice survive reboots

---

## Hardware

| Component | Detail |
|---|---|
| Microcontroller | Waveshare ESP32-S3-Zero |
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
| ⬜ White | Solid | Booting / connecting / OTA update in progress |
| 🔴 Red | Blinking | Connecting to Wi-Fi |
| 🔵 Blue | Blinking | Setup mode (captive portal active) |
| 🟢 Green | Solid | Playing — stream is running normally |
| 🟢 Green | Single flash | OTA update succeeded (reboots immediately after) |
| 🔴 Red | Solid (3 s) | OTA update failed |

---

## First-Time Setup

1. Power on the device.
2. If no Wi-Fi credentials are saved, it will blink **blue** and broadcast a hotspot named **`SearaBoomSetup`**.
3. Connect to that network from your phone or computer (no password required).
4. A browser should open automatically (captive portal). If not, navigate to **`http://4.3.2.1`**.
5. Choose your station, select your Wi-Fi network from the list, enter the password, and tap **Salvar**.
6. The device saves the settings and reboots. It will blink **red** while connecting, then go **solid green** when the stream starts.

> **Wi-Fi timeout:** If the device cannot connect within ~25 seconds, it automatically falls back to setup mode.

---

## OTA Updates

The device checks for updates on every boot after connecting to Wi-Fi.

**How it works:**
1. Fetches `version.txt` from this repository (`OTA_VERSION_URL`).
2. Compares the remote version string to the compiled `FIRMWARE_VERSION`.
3. If they differ, downloads `firmware.bin` from the matching GitHub Release and flashes it.
4. On success: green flash → automatic reboot into the new firmware.
5. On failure: red LED for 3 seconds → continues with existing firmware.

**To publish an update:**
1. Bump the version string in `version.txt` on the `main` branch.
2. Create a new GitHub Release with a tag matching that version string (e.g. `1.0.2`).
3. Attach the compiled `firmware.bin` to the release.

> OTA uses `WiFiClientSecure` with `setInsecure()` (no certificate pinning). For production use, replace this with a pinned root CA certificate.

---

## Arduino IDE Setup

### Required Libraries

- `ESP32-audioI2S` (schreibfaul1)
- `ESPAsyncWebServer`
- `AsyncTCP`
- `LittleFS` (bundled with ESP32 Arduino core)

### Tools Settings

| Setting | Value |
|---|---|
| Board | Waveshare ESP32-S3-Zero |
| USB CDC On Boot | Enabled |
| Partition Scheme | Custom (use `partitions.csv` below) |
| PSRAM | OPI PSRAM |
| Upload Speed | 921600 |

### Custom Partition Table

Create a file named `partitions.csv` in the same folder as the sketch:

```
# Name,   Type, SubType,  Offset,   Size,     Flags
nvs,      data, nvs,      0x9000,   0x5000,
otadata,  data, ota,      0xe000,   0x2000,
app0,     app,  ota_0,    0x10000,  0x1E0000,
app1,     app,  ota_1,    0x1F0000, 0x1E0000,
spiffs,   data, spiffs,   0x3D0000, 0x30000,
```

Then select **Tools → Partition Scheme → Custom**.

---

## Size Reduction (PlatformIO)

The firmware uses several techniques to fit within the 1.875 MB OTA partition:

| Technique | Saving |
|---|---|
| Bluetooth disabled | ~200–300 KB |
| IPv6 disabled | ~20–30 KB |
| Link Time Optimization (`-flto`) | ~10–20% |
| Nano newlib format | ~25–50 KB |
| HTML strings in `PROGMEM` | Reduces RAM usage |

Add these to `platformio.ini`:

```ini
build_flags =
    -DCONFIG_BT_ENABLED=0
    -DCONFIG_BLUEDROID_ENABLED=0
    -DCONFIG_NIMBLE_ENABLED=0
    -DCONFIG_LWIP_IPV6=0
    -Os
    -flto
```

> **Arduino IDE users:** Bluetooth cannot be disabled through the GUI alone. The simplest alternative is to use the 8 MB flash version of the ESP32-S3-Zero, which removes the size constraint entirely.

---

## Configuration Reference

Key constants at the top of the sketch:

| Constant | Default | Description |
|---|---|---|
| `FIRMWARE_VERSION` | `"1.0.1"` | Current firmware version string |
| `DEFAULT_VOLUME` | `8` | Volume on boot (range 1–21) |
| `TOUCHTHRESS` | `3500` | Capacitive touch sensitivity threshold |
| `DEBOUNCE` | `250` ms | Touch debounce interval |
| `WIFI_TIMEOUT` | `500` | Loop iterations before falling back to setup mode |
| `URL1` | Nova Russas stream | Primary station URL |
| `URL2` | Ibiapina stream | Secondary station URL |

---

## License

MIT — see `LICENSE` for details.
