# Building and Flashing

This project is built and flashed with Espressif ESP-IDF targeting the M5Stack StopWatch ESP32-S3.

## Current Build Environment

The current local build/flash workflow is using ESP-IDF **v5.5.4**.

Recent successful build output shows:

```text
NOTICE: [6/6] idf (5.5.4)
/Users/aaron/.espressif/v5.5.4/esp-idf/
```

Older project notes may mention ESP-IDF 5.4, but the current working environment is ESP-IDF 5.5.4.

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

The current root `CMakeLists.txt` still declares:

```cmake
project(StopWatch-UserDemo)
```

Because of that inherited project name, the generated application binary is currently named:

```text
build/StopWatch-UserDemo.bin
```

That name is expected for now and does not mean the Counter firmware failed to build.

A recent successful build produced an application binary around `0x2a6500` bytes, with the smallest app partition at `0x4f0000` bytes and about 46% free space remaining.

## Flash

```bash
idf.py flash
```

A recent successful flash used:

```text
Serial port /dev/cu.usbmodem101
Chip is ESP32-S3 (QFN56)
USB mode: USB-Serial/JTAG
Flash size: 16MB
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

## Expected Flash Layout

A typical flash command writes:

| Offset | Image |
| --- | --- |
| `0x0` | Bootloader |
| `0x8000` | Partition table |
| `0xd000` | OTA data |
| `0x20000` | Application binary |

Recent flash command shape:

```text
write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0 bootloader/bootloader.bin \
  0x20000 StopWatch-UserDemo.bin \
  0x8000 partition_table/partition-table.bin \
  0xd000 ota_data_initial.bin
```

ESP-IDF prints the exact command after a successful build.

## Managed Dependencies

The project uses ESP-IDF component-manager dependencies. Recent build output reported newer versions available for some managed dependencies, for example:

```text
Dependency "espressif/esp-dsp": "1.8.0" -> "1.8.2"
Dependency "espressif/esp_codec_dev": "1.5.4" -> "1.5.10"
Dependency "espressif/i2c_bus": "1.5.0" -> "1.5.2"
```

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
