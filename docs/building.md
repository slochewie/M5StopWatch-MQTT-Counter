# Building and Flashing

This project is built and flashed with Espressif ESP-IDF targeting the ESP32-S3.

## Prerequisites

- ESP-IDF v5.x
- Python 3
- Git
- USB-C connection to the M5Stack StopWatch

The current local workflow has been tested with ESP-IDF 5.x. Normal upstream compile warnings from inherited StopWatch demo code are not necessarily fatal if the build completes and produces a binary.

## Clone

```bash
git clone https://github.com/slochewie/M5StopWatch-MQTT-Counter.git
cd M5StopWatch-MQTT-Counter
```

## Configure Target

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

The current root `CMakeLists.txt` still declares:

```cmake
project(StopWatch-UserDemo)
```

Because of that inherited project name, the generated application binary is currently named:

```text
build/StopWatch-UserDemo.bin
```

That name is expected for now and does not mean the Counter firmware failed to build.

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

## Clean Build

Use a full clean when switching targets, changing major SDK configuration, or chasing stale generated artifacts:

```bash
idf.py fullclean
idf.py set-target esp32s3
idf.py build
```

## Expected Flash Layout

A typical flash command writes:

- Bootloader at `0x0`
- Partition table at `0x8000`
- OTA data at `0xd000`
- Application at `0x20000`

ESP-IDF prints the exact command after a successful build.

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
