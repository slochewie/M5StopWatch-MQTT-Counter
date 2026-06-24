Location Presets

This project supports predefined deployment locations (for example: Home, McCarthy’s, Library, Frog & Peach, and Bull’s) without committing real credentials to GitHub.

Overview

Real WiFi and MQTT connection details are stored in a local CSV file that is not tracked by Git.

A helper script converts that CSV into a generated C++ source file which is compiled into the firmware.

factory/location_presets.example.csv
            │
            ▼
Copy to:
factory/location_presets.private.csv
            │
            ▼
python tools/generate_location_presets.py
            │
            ▼
components/location_presets/location_presets_generated.cpp
            │
            ▼
idf.py build

Files

Tracked by Git:

* factory/location_presets.example.csv
* tools/generate_location_presets.py

Ignored by Git:

* factory/location_presets.private.csv
* components/location_presets/location_presets_generated.cpp

Create your private preset file

cp factory/location_presets.example.csv \
   factory/location_presets.private.csv

Edit factory/location_presets.private.csv with your real values.

Example format:

id,name,wifi_ssid,wifi_password,mqtt_uri
home,Home,HomeSSID,HomePassword,mqtt://192.168.1.10:1883
mccarthys,McCarthy's,VenueSSID,VenuePassword,mqtt://192.168.10.5:1883
library,Library,LibrarySSID,LibraryPassword,mqtt://192.168.20.5:1883
frog_peach,Frog & Peach,FrogSSID,FrogPassword,mqtt://192.168.30.5:1883
bulls,Bull's,BullsSSID,BullsPassword,mqtt://192.168.40.5:1883

Generate the source file

Run:

python tools/generate_location_presets.py

This produces:

components/location_presets/location_presets_generated.cpp

which is compiled into the firmware but is never committed to Git.

Building

After generating the source file, build normally:

idf.py build

or

idf.py flash monitor

Runtime behavior

The Settings → Location menu allows selecting one of the predefined locations.

Selecting a location updates the active configuration stored in NVS and reboots the device.

The following values are replaced:

* WiFi SSID
* WiFi password
* MQTT URI

The following values remain unchanged:

* Device name
* MQTT username
* MQTT password
* Counter topic(s)
* Any other unrelated device settings
