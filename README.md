# M5StopWatch MQTT Counter

MQTT-synchronized capacity counter firmware for the M5Stack StopWatch.

![Counter app launcher](docs/screenshots/launcher-counter-selected.png)

## Overview

M5StopWatch MQTT Counter turns the M5Stack StopWatch into a battery-powered venue capacity counter. The firmware adds a dedicated **Counter** app to the StopWatch launcher, displays the shared count on the round 466 × 466 AMOLED screen, and synchronizes count changes over MQTT.

The project is built with **ESP-IDF** and is based on the original M5Stack StopWatch User Demo firmware. The current firmware focuses on reliable physical-button counting, MQTT synchronization, long-press reset protection, battery publishing, AMOLED-friendly UI assets, and power-management work for hanging/lanyard use.

## Current Features

- Counter app integrated into the StopWatch launcher.
- AMOLED-optimized Counter launcher icon.
- Round LVGL Counter app UI with true-black background.
- Arc-top clock/status UI shared with the launcher style.
- Physical button controls:
  - `KEYB` / Go Next increments.
  - `KEYA` / Go Previous decrements.
- Touchscreen **HOLD RESET** button with approximately 2-second long-press protection.
- Count clamped at zero.
- Last count persisted to NVS.
- MQTT state synchronization through the configured counter state topic.
- State publish payload includes the device name.
- Incoming state accepts a plain integer or JSON with a `value` field.
- Battery percentage publishing on a derived battery topic.
- Settings app controls for Wi-Fi, MQTT, appliance mode, and startup app.
- Optional startup directly into the Counter app.
- 10-second display/network standby and 30-second ESP32-S3 deep-sleep path under active testing.

## User Interface

### Launcher

The Counter app appears in the StopWatch launcher carousel with the AMOLED-optimized Counter icon.

![Counter selected in launcher](docs/screenshots/launcher-counter-selected.png)

### Counter App

The Counter app shows the current count, a long-press reset button, battery level, MQTT connection state, and the active counter topic.

![Main counter screen](docs/screenshots/main-screen.png)

### Reset Protection

The touchscreen reset button is intentionally labeled **HOLD RESET**. A reset is only requested after the reset button is held for approximately two seconds.

![Reset screen](docs/screenshots/reset.png)

## Hardware

Target device:

- M5Stack StopWatch
- ESP32-S3
- Round 466 × 466 AMOLED touchscreen display
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

## MQTT Defaults

Default device/runtime configuration is defined in `components/device_config`.

| Setting | Default |
| --- | --- |
| Device name | `Capacity-01` |
| MQTT broker URI | `mqtt://smbhub.local:1883` |
| Counter state topic | `counters/capacity/state` |
| Battery topic | Derived from state topic: `counters/capacity/battery` |
| Time sync topic | `system/time/epoch` |

### State Payloads

The firmware publishes retained QoS 1 JSON state to the configured counter topic:

```json
{"value":30,"updated_by":"Capacity-01"}
```

Incoming counter state may be either a plain integer or JSON containing a `value` field.

### Battery Payloads

Battery publishes to the derived battery topic, for example:

```text
counters/capacity/battery
```

Payload:

```json
{"battery":84,"device":"Capacity-01"}
```

## System Architecture

```text
      ┌─────────────────┐
      │ StopWatch #1    │
      │ Counter app     │
      └───────┬─────────┘
              │ MQTT state/battery
              ▼
      ┌─────────────────┐
      │ MQTT Broker     │
      │ Mosquitto       │
      └───────┬─────────┘
              │
              ▼
      ┌─────────────────┐
      │ Node-RED        │
      │ authoritative   │
      │ counter logic   │
      └───────┬─────────┘
              │ retained state
      ┌───────┴──────────────┐
      ▼                      ▼
StopWatch #2           Web dashboard /
Other clients          other MQTT clients
```

Node-RED remains the recommended authoritative owner of the shared counter value. StopWatch devices subscribe to the state topic and publish updated values when local button or reset actions occur.

See [`docs/architecture.md`](docs/architecture.md) and [`docs/mqtt.md`](docs/mqtt.md) for more detail.

## Screenshots

| Screen | Preview |
| --- | --- |
| Launcher, Counter selected | ![Launcher Counter selected](docs/screenshots/launcher-counter-selected.png) |
| Counter app | ![Counter app](docs/screenshots/main-screen.png) |
| Increment | ![Increment](docs/screenshots/increment.png) |
| Decrement | ![Decrement](docs/screenshots/decrement.png) |
| Reset | ![Reset](docs/screenshots/reset.png) |

## Documentation

- [Architecture](docs/architecture.md)
- [Wi-Fi Architecture](docs/wifi.md)
- [MQTT Integration](docs/mqtt.md)
- [Building and Flashing](docs/building.md)
- [Power Management](docs/power-management.md)
- [Screenshots](docs/screenshots/README.md)

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

Current build output still uses the inherited ESP-IDF project name `StopWatch-UserDemo` because the root `CMakeLists.txt` still declares `project(StopWatch-UserDemo)`.

## Repository Structure

```text
M5StopWatch-MQTT-Counter/
├── main/
│   ├── apps/
│   │   ├── app_counter/
│   │   ├── app_launcher/
│   │   ├── app_setup/
│   │   └── common/
│   └── assets/
│       └── images/
├── components/
│   ├── counter_service/
│   └── device_config/
├── docs/
│   ├── architecture.md
│   ├── building.md
│   ├── mqtt.md
│   ├── power-management.md
│   └── screenshots/
├── CMakeLists.txt
├── sdkconfig.defaults
└── README.md
```

## Power Management Status

Power management is active development but no longer purely theoretical.

Current behavior in the system sleep manager:

- 10 seconds idle while hanging: display/network standby.
- 30 seconds idle while hanging: ESP32-S3 native deep sleep target.
- Display standby turns off the backlight, sleeps the display, and pauses Wi-Fi/MQTT recovery.
- Standby wakes from touch or physical button activity.
- Deep sleep is configured for touch wake on GPIO13 / `G13_TP_INT`.
- Network recovery is deferred until needed after wake.

See [`docs/power-management.md`](docs/power-management.md) for details and known limitations.

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
- AMOLED-optimized launcher artwork
- MQTT synchronization reliability
- Long-press reset safety
- Battery publishing
- Settings controls for Wi-Fi/MQTT/startup behavior
- Round-display UI documentation
- Sleep/wake architecture testing
