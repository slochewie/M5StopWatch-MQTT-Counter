# M5StopWatch MQTT Counter

MQTT-synchronized capacity counter firmware for the M5Stack StopWatch.

![Counter app launcher](docs/screenshots/launcher-counter-selected.svg)

## Overview

M5StopWatch MQTT Counter turns the M5Stack StopWatch into a battery-powered capacity counter that stays synchronized with other StopWatch devices, web dashboards, Node-RED, and other MQTT clients through a central MQTT broker.

The firmware is based on the original M5Stack StopWatch User Demo, but the launcher now includes a dedicated **Counter** app with MQTT state synchronization, battery publishing, a round LVGL user interface, and safer long-press reset behavior.

## Current Features

- MQTT-synchronized counter state
- Multiple StopWatch devices can share one counter value
- Increment and decrement using the physical StopWatch buttons
- Touchscreen reset button with long-press protection
- Counter value pulled from MQTT before local increment/decrement
- Count is clamped at zero when decrementing
- Battery percentage publishing
- MQTT status and topic shown on the Counter app screen
- Round LVGL interface for the 466 × 466 StopWatch display
- Counter launcher icon using the mechanical tally-counter asset
- Initial display/light-sleep and IMU wake infrastructure under development

## User Interface

### Launcher

The Counter app appears in the StopWatch launcher carousel with the mechanical tally-counter icon.

![Counter selected in launcher](docs/screenshots/launcher-counter-selected.svg)

### Counter App

The Counter app shows the current count, a long-press reset button, battery level, MQTT connection state, and the active counter topic.

![Main counter screen](docs/screenshots/main-screen.svg)

### Reset Protection

The touchscreen reset button is intentionally labeled **HOLD RESET**. A reset is only requested after the reset button is held for approximately two seconds. This prevents accidental resets from a quick tap.

![Reset screen](docs/screenshots/reset.svg)

## Hardware

Target device:

- M5Stack StopWatch
- ESP32-S3
- Round 466 × 466 touchscreen display
- Integrated battery
- IMU motion sensor
- Physical KEYA and KEYB buttons

## Controls

| Control | Function |
| --- | --- |
| KEYB / Go Next | Increment |
| KEYA / Go Previous | Decrement |
| Touchscreen **HOLD RESET** button | Reset after long press |
| Home gesture/button event | Return to launcher |

## MQTT Topics

Default topic family:

```text
counters/capacity
```

| Topic | Direction | Purpose |
| --- | --- | --- |
| `counters/capacity/state` | Subscribe/publish | Shared authoritative counter value |
| `counters/capacity/increment` | Publish/consume by Node-RED flow | Increment command, if using command topics |
| `counters/capacity/decrement` | Publish/consume by Node-RED flow | Decrement command, if using command topics |
| `counters/capacity/reset` | Publish/consume by Node-RED flow | Reset command, if using command topics |
| `counters/<device>/battery` | Publish | Device battery percentage |

The Counter app displays the active state topic on screen and uses the MQTT service to synchronize the latest value before applying local button changes.

## System Architecture

```text
                    ┌───────────────┐
                    │   Node-RED    │
                    │ Authoritative │
                    │ Counter State │
                    └───────┬───────┘
                            │
                      MQTT Broker
                            │
         ┌──────────────────┼──────────────────┐
         │                  │                  │
         ▼                  ▼                  ▼
  StopWatch #1      StopWatch #2       Web Dashboard
         │                  │                  │
         ▼                  ▼                  ▼
   Battery topic      Battery topic     Other MQTT clients
```

Node-RED remains the recommended authoritative counter-state owner. StopWatch devices subscribe to the shared state topic and publish updates when local button or reset actions occur.

See [`docs/architecture.md`](docs/architecture.md) for more detail.

## Screenshots

| Screen | Preview |
| --- | --- |
| Launcher, Counter selected | ![Launcher Counter selected](docs/screenshots/launcher-counter-selected.svg) |
| Counter app | ![Counter app](docs/screenshots/main-screen.svg) |
| Increment | ![Increment](docs/screenshots/increment.svg) |
| Decrement | ![Decrement](docs/screenshots/decrement.svg) |
| Reset | ![Reset](docs/screenshots/reset.svg) |
| Diagnostics mockup | ![Diagnostics](docs/screenshots/diagnostics.svg) |
| MQTT sync concept | ![MQTT sync](docs/screenshots/mqtt-sync.svg) |
| Sleep mode concept | ![Sleep mode](docs/screenshots/sleep-mode.svg) |

## Development Environment

This project is developed and flashed with Espressif ESP-IDF.

### Requirements

- ESP-IDF v5.x
- Python 3.x
- Git
- USB-C connection to the M5Stack StopWatch

### Clone Repository

```bash
git clone https://github.com/slochewie/M5StopWatch-MQTT-Counter.git
cd M5StopWatch-MQTT-Counter
```

### Configure Target

```bash
idf.py set-target esp32s3
```

Optional:

```bash
idf.py menuconfig
```

### Build

```bash
idf.py build
```

### Flash

```bash
idf.py flash
```

### Monitor

```bash
idf.py monitor
```

Flash and monitor:

```bash
idf.py flash monitor
```

Current build output may still use the inherited application binary name `StopWatch-UserDemo.bin` until the ESP-IDF project metadata is renamed.

## Repository Structure

```text
M5StopWatch-MQTT-Counter/
├── main/
│   ├── apps/
│   │   ├── app_counter/
│   │   └── app_launcher/
│   └── assets/
│       └── images/
├── components/
├── docs/
│   ├── architecture.md
│   └── screenshots/
├── CMakeLists.txt
├── sdkconfig.defaults
└── README.md
```

## Power Management Status

Power management is still active development.

Current implementation notes:

- Display/light-sleep helper code exists in the Counter app.
- IMU-orientation wake sampling exists for future sleep behavior.
- App-owned automatic display sleep timeout is currently disabled while the system sleep manager work is being sorted out.
- Wake recovery schedules MQTT/network recovery after display sleep wake.

## Related Projects

### Original Firmware

- [M5Stack StopWatch User Demo](https://github.com/m5stack/M5StopWatch-UserDemo)

### Companion Projects

- [M5StickS3 MQTT Counter](https://github.com/slochewie/M5StickS3-MQTT-Counter)
- Node-RED Capacity Counter
- Counter Web Dashboard
- GeekMagic SmallTV Pro MQTT Display

## Status

Active development.

Recent focus:

- Counter launcher integration
- MQTT synchronization reliability
- Long-press reset safety
- Battery publishing
- Round-display UI documentation
- Sleep/wake architecture research
