# Moon Lander for Adafruit RP2040 + OLED display

A lunar lander game for the Adafruit Feather RP2040 with a 128×64 OLED FeatherWing (SH1107).

## Demo

[![Gameplay demo](https://img.youtube.com/vi/FGu6VVlrhw0/hqdefault.jpg)](https://www.youtube.com/watch?v=FGu6VVlrhw0)

|  |  |
|---|---|
| <img width="612" height="299" alt="image" src="https://github.com/user-attachments/assets/0bc2931c-6102-4c6c-aa04-d62929a3e976" /> | <img width="624" height="301" alt="image" src="https://github.com/user-attachments/assets/a79be884-8adb-4ab8-868d-3d818fe6f3a7" /> |
| <img width="612" height="304" alt="image" src="https://github.com/user-attachments/assets/fea8a6c3-3f26-4e31-84ad-e3234ac3ef4b" /> |<img width="603" height="296" alt="image" src="https://github.com/user-attachments/assets/ca361bf4-e757-4f09-86bb-2887706dbe31" /> |

## Hardware

| Part | Detail |
|------|--------|
| MCU | [Adafruit Feather RP2040](https://www.adafruit.com/product/4884) |
| Display | [128×64 OLED FeatherWing](https://www.adafruit.com/product/4650) (SH1107) |
| Input | Wing buttons A, B, C |
| Feedback | On-board NeoPixel |

Stack the OLED FeatherWing on the Feather RP2040. Connect over USB with a data cable.

## Software

- **Arduino IDE**
- **Board package:** Raspberry Pi Pico/RP2040 by **Earle Philhower**
- **Board:** Adafruit Feather RP2040

### Libraries

Install via Library Manager:

- Adafruit SH110x
- Adafruit GFX Library
- Adafruit BusIO
- Adafruit NeoPixel

## Upload

1. Open `rp2040_moon_lander.ino` in the Arduino IDE.
2. Select **Adafruit Feather RP2040** and the correct USB port (`/dev/cu.usbmodem*` on macOS).
3. Upload.

If serial upload fails, enter the bootloader (hold **BOOT**, tap **RESET**) and drag a `.uf2` build onto the `RPI-RP2` drive.

## Controls

| Button | Action |
|--------|--------|
| **A** | Rotate left |
| **C** | Rotate right |
| **B** | Thrust (uses fuel) |
| **A / B / C** | Start game or retry (title / result screens) |

Land on the flat pad slowly and upright. Too fast, too tilted, or rough ground = crash.

The HUD shows remaining fuel (`F:`) and a vertical-speed meter on the right (safe landing threshold marked).

## Setup

Feather RP2040 with OLED FeatherWing stacked, connected over USB.

![Feather RP2040 + OLED FeatherWing](docs/setup.png)
