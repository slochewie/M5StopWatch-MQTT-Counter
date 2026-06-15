# Building

This project is built with Espressif ESP-IDF targeting the ESP32-S3.

## Prerequisites
- ESP-IDF v5.x
- Python 3
- Git
- USB connection to the M5Stack StopWatch

## Clone
```bash
git clone https://github.com/slochewie/M5StopWatch-MQTT-Counter.git
cd M5StopWatch-MQTT-Counter
```

## Configure
```bash
idf.py set-target esp32s3
```

## Build
```bash
idf.py build
```

## Flash
```bash
idf.py flash
```

## Monitor
```bash
idf.py monitor
```

## Clean Build
```bash
idf.py fullclean
idf.py build
```

## Notes
Current build artifacts may still use the inherited binary name `StopWatch-UserDemo.bin`.