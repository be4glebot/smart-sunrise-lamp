# Smart Sunrise Alarm Lamp

A Wi-Fi alarm lamp built on an ESP32. At a set wake-up time it lights an addressable LED strip, optionally brightening gradually like a sunrise, and it is controlled from a phone-friendly web page served by the ESP32 itself. No app is needed.

Built for a deep sleeper who wears earplugs: light gets through where sound doesn't.

## Features

- **Alarm time and color** set from a web page on your phone, saved to flash so settings survive power loss
- **Gradual sunrise** (5 to 30 minutes) that reaches full brightness at the alarm time, plus a 20-second preview
- **Auto shut-off** after 30 minutes at full brightness
- **Physical button** to turn the lamp on and off
- **NTP time sync** with Central time and daylight saving
- **`lamp.local`** address via mDNS, with the numeric address printed over serial as a fallback
- Status pills update in the background, and buttons don't reload the page

## Hardware

| Part | Notes |
|---|---|
| ESP32 dev board (ELEGOO ESP-32 kit, CP2102 USB chip) | 2.4 GHz Wi-Fi only |
| WS2812B addressable LED strip, 5V, 60 LEDs/m | first 120 LEDs used (2 m, along a full-size headboard with a wrap down each side); cut at a 50 cm joint |
| 5V 3A power adapter | powers the strip only |
| 330 ohm resistor | on the data line |
| Momentary push button | between GPIO 13 and GND |

### Wiring

| From | To |
|---|---|
| Strip +5V (red) | Adapter + |
| Strip GND (white) | Adapter - |
| Adapter - | ESP32 GND (shared ground) |
| Strip data (green) | 330 ohm resistor, then ESP32 GPIO 4 |
| Button | ESP32 GPIO 13 and GND |

The ESP32 is powered over USB and the strip from the adapter; only the grounds are shared.

## Setup

1. Install the Arduino IDE and the **esp32 by Espressif Systems** board package. Select **ESP32 Dev Module**.
2. Install the **Adafruit NeoPixel** library.
3. Open `SunriseLamp/SunriseLamp.ino` and set `WIFI_SSID` and `WIFI_PASSWORD` at the top.
4. Change `TIMEZONE` if you are not in US Central time.
5. Upload, open the Serial Monitor at 115200 baud, and note the address it prints.
6. Open `http://lamp.local` (or the printed address) on a phone on the same Wi-Fi.

Settings you can change in the sketch: `LED_COUNT`, `BRIGHTNESS` (0 to 255), `AUTO_OFF_MINUTES`, `LED_PIN`, `BUTTON_PIN`.

## Notes

- The ESP32 only supports 2.4 GHz Wi-Fi. Some managed networks (such as apartment Wi-Fi) isolate devices from each other, which can stop a phone from reaching the lamp.
- Keep the strip stretched out or loosely wrapped while powered, since coiled strips shed heat poorly. With 120 LEDs, keep `BRIGHTNESS` at 90 or lower so a 3 A adapter has headroom (worst case, all white, is about 2.5 A at 90).

## Tech

ESP32, C++ (Arduino framework), Wi-Fi, HTTP server, NTP, mDNS, flash storage (Preferences), HTML/CSS/JavaScript, WS2812B LEDs
