# M5StopWatch MQTT Counter Architecture

## Overview

The firmware adds a dedicated Counter app to the M5Stack StopWatch launcher. The Counter app displays the shared count, handles physical/touch input, persists the last value locally, and synchronizes with MQTT.

Node-RED remains the recommended authoritative owner of counter state in a multi-device deployment.

## Main Components

### M5Stack StopWatch firmware

Current firmware responsibilities:

- Show the Counter app in the launcher.
- Open the Counter app directly at boot when Startup App is set to Counter.
- Read KEYA / KEYB button events.
- Require long press for touchscreen reset.
- Persist last counter value to NVS.
- Connect to Wi-Fi/MQTT when enabled.
- Subscribe to counter state and time topics.
- Publish counter state and battery status.
- Enter display/network standby and ESP32-S3 deep sleep under the current sleep-manager rules.

### MQTT broker

Mosquitto is the expected broker in the current deployment, but the firmware uses a generic MQTT URI and can connect to another broker if configured.

Default broker URI:

```text
mqtt://smbhub.local:1883
```

### Node-RED

Node-RED should maintain the authoritative count, clamp it at zero, and republish retained state so newly booted devices immediately receive the current value.

### Other clients

Other MQTT clients can include:

- `counter.html` web dashboard
- M5StickS3 MQTT Counter
- GeekMagic SmallTV Pro display
- Future mobile or venue dashboards

## Data Flow

```text
       KEYA / KEYB / HOLD RESET
                │
                ▼
      ┌───────────────────┐
      │ M5Stack StopWatch │
      │ Counter app       │
      └─────────┬─────────┘
                │ retained JSON state
                ▼
      ┌───────────────────┐
      │ MQTT broker       │
      └─────────┬─────────┘
                │
                ▼
      ┌───────────────────┐
      │ Node-RED          │
      │ authoritative     │
      │ counter logic     │
      └─────────┬─────────┘
                │ retained state
      ┌─────────┼───────────────┐
      ▼         ▼               ▼
StopWatch #2  Web dashboard   Other MQTT clients
```

## Current MQTT Topics

| Topic | Purpose |
| --- | --- |
| `counters/capacity/state` | Default shared counter state topic. |
| `counters/capacity/battery` | Default derived battery topic. |
| `system/time/epoch` | Optional time synchronization topic. |

The battery topic is derived from the configured state topic. For example, `counters/front-door/state` becomes `counters/front-door/battery`.

## Published State Payload

The StopWatch publishes retained QoS 1 JSON state:

```json
{"value":30,"updated_by":"Capacity-01"}
```

The firmware can also consume older/simple payloads such as:

```text
30
```

or:

```json
{"value":30}
```

## Settings and Appliance Behavior

The Settings app includes runtime controls for:

- Wi-Fi on/off
- MQTT on/off
- Appliance mode on/off
- Startup app: Counter or Launcher

When Startup App is set to Counter, the launcher opens the Counter app automatically during startup.

## Power State Architecture

The current sleep manager owns the app-level sleep behavior.

- After 10 seconds idle while hanging: display/network standby.
- After 30 seconds idle while hanging: ESP32-S3 deep sleep target.
- Standby wakes from touch or physical buttons.
- Deep sleep is configured for EXT0 touch wake on GPIO13 / `G13_TP_INT`.
- Network recovery is deferred until requested after wake.

See [`power-management.md`](power-management.md) for details.
