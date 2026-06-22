# MQTT Integration

This firmware uses MQTT to keep the M5Stack StopWatch Counter app synchronized with Node-RED, web dashboards, and other MQTT clients.

## Current Role of the StopWatch

The StopWatch is an MQTT client. It:

- Loads device, broker, credential, and topic settings from NVS through `device_config`.
- Connects to Wi-Fi and MQTT when those settings are enabled.
- Subscribes to the configured counter state topic.
- Subscribes to `system/time/epoch` for time synchronization.
- Publishes retained counter state when local button or reset actions occur.
- Publishes retained battery status to a topic derived from the counter state topic.

Node-RED is still the recommended authoritative owner of the shared count when multiple clients are involved.

## Default Runtime Configuration

| Setting | Default |
| --- | --- |
| Device name | `Capacity-01` |
| MQTT broker URI | `mqtt://smbhub.local:1883` |
| Counter state topic | `counters/capacity/state` |
| Battery topic | `counters/capacity/battery` |
| Time topic | `system/time/epoch` |

The battery topic is derived automatically from the configured counter topic. If the counter topic ends in `/state`, that suffix is replaced with `/battery`.

Examples:

| Counter topic | Derived battery topic |
| --- | --- |
| `counters/capacity/state` | `counters/capacity/battery` |
| `counters/front-door/state` | `counters/front-door/battery` |
| `counters/capacity` | `counters/capacity/battery` |

## Counter State Topic

Default:

```text
counters/capacity/state
```

### Published Payload

The firmware publishes retained QoS 1 JSON:

```json
{"value":30,"updated_by":"Capacity-01"}
```

`updated_by` is the configured device name, or `m5stopwatch` if the device name is empty.

### Accepted Incoming Payloads

The firmware accepts either a plain integer:

```text
30
```

or JSON containing a `value` field:

```json
{"value":30}
```

Negative values are clamped to zero.

## Battery Topic

Default derived topic:

```text
counters/capacity/battery
```

Published retained QoS 1 payload:

```json
{"battery":84,"device":"Capacity-01"}
```

Battery publishing behavior:

- The Counter app asks to publish battery status when it opens.
- The app checks battery status while running.
- The service publishes when the percentage changes.
- The service also has a heartbeat interval so unchanged battery values can still be republished periodically.
- Battery publishing is skipped while Wi-Fi/MQTT are unavailable.

## Time Synchronization Topic

The firmware subscribes to:

```text
system/time/epoch
```

Accepted payloads:

```text
1719871234
```

or:

```json
{"epoch":1719871234}
```

or:

```json
{"value":1719871234}
```

The received epoch is sanity-checked, applied to system time, synced to the RTC, and interpreted with the firmware timezone setting.

## Counter App Behavior

Before applying a local increment or decrement, the app attempts to consume the latest received MQTT value. This reduces stale updates when several devices are active.

Current local actions:

| Action | Behavior |
| --- | --- |
| Increment | Pull latest MQTT value if available, add one, persist to NVS, publish JSON state. |
| Decrement | Pull latest MQTT value if available, subtract one if above zero, persist to NVS, publish JSON state. |
| Reset | Set zero, persist to NVS, publish JSON state. |
| Reset input | Requires approximately 2 seconds of touchscreen long press. |

If a publish fails because MQTT/network is unavailable, the app keeps a pending publish value and retries after requesting network recovery.

## Settings Controls

The Settings app currently exposes runtime toggles for:

- Wi-Fi on/off
- MQTT on/off
- Appliance mode on/off
- Startup app: Counter or Launcher

Disabling Wi-Fi or MQTT pauses recovery and prevents new publishes until re-enabled.

## Recommended Node-RED Behavior

Node-RED should:

1. Subscribe to the configured state topic.
2. Accept either plain integer state or JSON with `value` if bridging older clients.
3. Maintain the authoritative counter value.
4. Clamp the value at zero.
5. Publish the accepted state back to the state topic as a retained message.
6. Preserve compatibility with web dashboards and other clients.

## Multi-Device Synchronization

All StopWatch devices that should share a count must use the same counter state topic.

Example:

```text
counters/capacity/state
```

Each device publishes its own `updated_by` field, but all devices consume the same shared state value.

## Future Multi-Counter Convention

For multiple counters, use a counter-name segment:

```text
counters/<counter_name>/state
counters/<counter_name>/battery
```

Examples:

```text
counters/front-door/state
counters/front-door/battery
counters/back-door/state
counters/back-door/battery
```
