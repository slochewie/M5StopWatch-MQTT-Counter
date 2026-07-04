# M5StopWatch MQTT Counter Architecture

## Overview

M5StopWatch MQTT Counter adds a dedicated **Counter** app to the M5Stack StopWatch firmware. The app turns the StopWatch into a handheld/lanyard-friendly venue capacity counter with physical button input, a round AMOLED UI, MQTT synchronization, battery publishing, and configurable power-management behavior for idle hanging use.

The project is designed around a shared MQTT counter state. In a multi-device deployment, Node-RED remains the recommended authoritative owner of the count. StopWatch devices publish local changes and subscribe to the shared retained state so each device, dashboard, and display client stays synchronized.

## High-Level Architecture

```text
          KEYA / KEYB / HOLD RESET
                    │
                    ▼
        ┌────────────────────────┐
        │ M5Stack StopWatch      │
        │ Counter app            │
        │ - local UI             │
        │ - NVS last value       │
        │ - MQTT client          │
        │ - battery publisher    │
        └───────────┬────────────┘
                    │ retained MQTT JSON
                    ▼
        ┌────────────────────────┐
        │ MQTT broker            │
        │ Mosquitto / compatible │
        └───────────┬────────────┘
                    │
                    ▼
        ┌────────────────────────┐
        │ Node-RED               │
        │ authoritative counter  │
        │ clamp / retain / fanout│
        └───────────┬────────────┘
                    │ retained shared state
        ┌───────────┼───────────────────┐
        ▼           ▼                   ▼
 StopWatch #2   Web dashboard      Other MQTT clients
 StopWatch #3   counter.html       companion displays
```

## Firmware Components

### Launcher

The launcher owns the app carousel and displays the Counter app with the AMOLED-optimized Counter icon. On startup, it can optionally open the Counter app directly when the Settings app has Startup App set to Counter.

### Counter App

The Counter app is the primary user-facing app.

Responsibilities:

- Display the current count on the 466 × 466 round AMOLED screen.
- Render a true-black LVGL UI with the shared arc-top clock/status style.
- Handle `KEYB` / Go Next as increment.
- Handle `KEYA` / Go Previous as decrement.
- Provide a touchscreen **HOLD RESET** button requiring about a 2-second press.
- Clamp count values at zero.
- Persist the latest local count to NVS.
- Pull the latest received MQTT value before local increment/decrement when available.
- Publish new count values through `counter_service`.
- Publish battery percentage while running.
- Avoid normal foreground MQTT/UI work while the system sleep manager reports a sleep state.

### Counter Service

`counter_service` provides the app-facing MQTT integration.

Responsibilities:

- Load runtime configuration from `device_config` / NVS.
- Start Wi-Fi and MQTT when enabled.
- Subscribe to the configured counter state topic.
- Subscribe to `system/time/epoch` for time sync.
- Parse incoming counter payloads.
- Publish retained QoS 1 counter state JSON.
- Derive and publish the battery topic.
- Expose status text and current topic information for the Counter app UI.
- Respect Settings app Wi-Fi/MQTT enable state.

### Device Config

`device_config` stores runtime defaults and NVS-backed settings.

Current defaults:

| Setting | Default |
| --- | --- |
| Device name | `Capacity-01` |
| MQTT broker URI | `mqtt://smbhub.local:1883` |
| Counter state topic | `counters/capacity/state` |

### Settings App

The Settings app exposes runtime controls that affect the architecture:

- Wi-Fi on/off
- MQTT on/off
- Wake Settings: configurable Soft Sleep and Deep Sleep timeouts
- Appliance Mode on/off
- Startup App: Counter or Launcher
- Brightness, volume, button settings, time/date, and firmware/about screens inherited from the base firmware

When Wi-Fi or MQTT is disabled, recovery is paused and counter publishes are skipped until re-enabled.

## MQTT Topics

| Topic | Direction | Purpose |
| --- | --- | --- |
| `counters/capacity/state` | Subscribe + publish | Default shared counter state topic. |
| `counters/capacity/battery` | Publish | Default derived battery topic. |
| `system/time/epoch` | Subscribe | Optional epoch time synchronization topic. |

The battery topic is derived from the configured counter topic. If the configured counter topic ends with `/state`, that suffix is replaced by `/battery`.

Examples:

| Counter topic | Battery topic |
| --- | --- |
| `counters/capacity/state` | `counters/capacity/battery` |
| `counters/front-door/state` | `counters/front-door/battery` |
| `counters/capacity` | `counters/capacity/battery` |

## MQTT Payloads

### Published Counter State

The StopWatch publishes retained QoS 1 JSON to the configured counter state topic:

```json
{"value":30,"updated_by":"Capacity-01"}
```

`updated_by` comes from the configured device name.

### Accepted Counter State

For compatibility, the firmware accepts either a plain integer:

```text
30
```

or JSON containing a `value` field:

```json
{"value":30}
```

Negative values are clamped to zero.

### Published Battery State

Battery state is published as retained QoS 1 JSON:

```json
{"battery":84,"device":"Capacity-01"}
```

### Time Sync

The firmware subscribes to:

```text
system/time/epoch
```

Accepted payloads include a plain epoch value or JSON with `epoch` / `value`.

## Multi-Device Synchronization

All devices that should share a count use the same configured state topic. A button press on one StopWatch publishes a new state value. Node-RED should validate, clamp, retain, and republish that accepted value. Other StopWatch devices and dashboards receive the retained update and refresh their displays.

```text
StopWatch #1 increment
        │
        ▼
 publishes {"value":31,"updated_by":"Capacity-01"}
        │
        ▼
 MQTT broker / Node-RED authoritative logic
        │
        ▼
 retained state fanout
        │
        ├── StopWatch #2 updates to 31
        ├── StopWatch #3 updates to 31
        └── Web dashboard updates to 31
```

## Power State Architecture

Power management is owned by the system sleep manager rather than by the Counter app alone.

Current behavior:

- Soft Sleep timeout is configurable from Settings and defaults to 15 seconds.
- Deep Sleep timeout is configurable from Settings and defaults to 45 seconds.
- Both sleep stages are gated by hanging/lanyard orientation.
- Soft Sleep turns off the backlight, sleeps the display, pauses MQTT/Wi-Fi recovery, and stops Wi-Fi.
- Soft Sleep wakes from touch or physical button activity.
- Deep Sleep is configured for EXT0 touch wake on GPIO13 / `G13_TP_INT`.
- Network recovery is deferred until requested after wake.
- Counter publishes request network resume when needed.

See [`power-management.md`](power-management.md) for details and current limitations.

## Companion Clients

The architecture allows other MQTT clients to participate as long as they use compatible topics and payloads.

Known companion/project clients include:

- `counter.html` web dashboard
- M5StickS3 MQTT Counter
- Node-RED dashboard/flow UI
- GeekMagic SmallTV Pro MQTT Display
- Future venue/mobile dashboards

## Design Intent

The intended deployment is a venue capacity counter where one or more StopWatch devices are used by staff at doors or checkpoints. Node-RED owns the authoritative count, MQTT provides synchronization, and the StopWatch firmware provides a compact, battery-powered, physical-button interface optimized for the round AMOLED hardware.
