# Location preset NVS

Location presets are stored in a separate `preset_nvs` partition so real WiFi passwords and broker URIs do not need to be committed to GitHub.

Committed file:

- `location_presets.example.csv` - fake values and required key names

Private/generated files, ignored by Git:

- `location_presets.private.csv` - your real values
- `location_presets.bin` - generated NVS image flashed to `preset_nvs`

## Create your private CSV

```sh
cp factory/location_presets.example.csv factory/location_presets.private.csv
```

Edit `factory/location_presets.private.csv` with the real SSID, WiFi password, and MQTT URI for each location.

Do not change the namespace or key names unless you also update `components/location_presets/location_presets.cpp`.

## Generate the NVS image

```sh
python "$IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py" generate \
  factory/location_presets.private.csv \
  factory/location_presets.bin \
  0x6000
```

## Flash only the preset partition

Replace the serial port with your StopWatch port.

```sh
python "$IDF_PATH/components/partition_table/parttool.py" \
  --port /dev/cu.usbmodemXXXX \
  write_partition \
  --partition-name preset_nvs \
  --input factory/location_presets.bin
```

After flashing, open Settings > Location on the StopWatch. Selecting a location copies that preset into the normal Configure NVS values and reboots the device.

Only these active values are overwritten:

- WiFi SSID
- WiFi password
- MQTT URI

These existing Configure values are preserved:

- Device name
- MQTT username
- MQTT password
- Counter topic
