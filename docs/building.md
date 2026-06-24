# Building and Flashing

This project is built and flashed with Espressif ESP-IDF targeting the M5Stack StopWatch ESP32-S3.

## Current Build Environment

The current local build/flash workflow is using ESP-IDF **v5.5.4**.

## Prerequisites

- ESP-IDF v5.5.x recommended based on the current working setup
- Python 3
- Git
- USB-C connection to the M5Stack StopWatch
- macOS, Linux, or another ESP-IDF-supported build host

Normal upstream compile warnings from inherited StopWatch demo code are not necessarily fatal if the build completes and produces a binary.

## Clone

```bash
git clone https://github.com/slochewie/M5StopWatch-MQTT-Counter.git
cd M5StopWatch-MQTT-Counter
```

## Configure Target

The StopWatch uses an ESP32-S3, so set the ESP-IDF target to `esp32s3`:

```bash
idf.py set-target esp32s3
```

Optional configuration menu:

```bash
idf.py menuconfig
```

## Build

```bash
idf.py build
```

The generated application binary is named from the project declaration in the root `CMakeLists.txt`:

```text
build/StopWatch-MQTT-Counter.bin
```

## Flash

```bash
idf.py flash
```

If ESP-IDF does not automatically choose the correct serial port, specify it explicitly:

```bash
idf.py -p /dev/cu.usbmodem101 flash
```

On macOS, the exact device path may differ.

## Monitor

```bash
idf.py monitor
```

Flash and monitor in one command:

```bash
idf.py flash monitor
```

Exit the monitor with the normal ESP-IDF monitor escape sequence, usually `Ctrl+]`.

## Clean Build

Use a full clean when switching targets, changing major SDK configuration, or chasing stale generated artifacts:

```bash
idf.py fullclean
idf.py set-target esp32s3
idf.py build
```

## Flash Layout

ESP-IDF prints the exact flash command after a successful build. In normal use, prefer `idf.py flash` instead of copying the generated `esptool.py` command manually.

## Managed Dependencies

The project uses ESP-IDF component-manager dependencies.

Do not update dependencies casually if the firmware is building and flashing correctly. To intentionally refresh the lock file for testing, use:

```bash
idf.py update-dependencies
idf.py build
```

Only commit dependency lock changes after testing the firmware on hardware.

## Runtime Configuration

Network and MQTT runtime defaults live in `components/device_config`.

Current defaults:

| Setting | Default |
| --- | --- |
| Device name | `Capacity-01` |
| MQTT broker URI | `mqtt://smbhub.local:1883` |
| Counter state topic | `counters/capacity/state` |

Runtime configuration is stored in NVS. The Settings app also exposes toggles for Wi-Fi, MQTT, appliance mode, and startup app behavior.

## Quick Command Set

```bash
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```
