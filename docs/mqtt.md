# MQTT Integration

This firmware uses MQTT to keep the M5Stack StopWatch Counter app synchronized with Node-RED, web dashboards, and other MQTT clients.

## Recommended Architecture

Node-RED should remain the authoritative owner of counter state.

```text
StopWatch button/touch input
        │
        ▼
MQTT broker ─── Node-RED authoritative counter logic
        │
        ├── StopWatch #1
        ├── StopWatch #2
        ├── Web dashboard
        └── Other display clients
```

The StopWatch subscribes to the current counter state and publishes updates when local user actions occur.

## Default Topic Family

The current default counter topic family is:

```text
counters/capacity
```

## Topics

| Topic | Payload | Purpose |
| --- | --- | --- |
| `counters/capacity/state` | Integer, for example `30` | Shared counter state. All clients should subscribe to this topic. |
| `counters/capacity/increment` | Usually `1` | Optional command topic used by Node-RED or web clients to request an increment. |
| `counters/capacity/decrement` | Usually `1` | Optional command topic used by Node-RED or web clients to request a decrement. |
| `counters/capacity/reset` | Usually `1` | Optional command topic used by Node-RED or web clients to request a reset. |
| `counters/<device>/battery` | Integer percentage, `0` to `100` | Battery level published by each device. |

The Counter app displays the active state topic on the device screen.

## Counter State Behavior

Before applying a local increment or decrement, the Counter app attempts to pull the latest MQTT value from the counter service. This reduces the chance of stale local changes when multiple devices are being used.

Current app behavior:

- Increment adds one to the latest known value.
- Decrement subtracts one, but does not go below zero.
- Reset sets the value to zero.
- Reset is protected by a long-press touchscreen action.

## Battery Publishing

The Counter app publishes battery percentage periodically and when the value changes. The recommended topic format is:

```text
counters/<device>/battery
```

Examples:

```text
counters/M5StopWatch-01/battery
counters/M5StopWatch-02/battery
```

Payload example:

```text
84
```

## Node-RED Expectations

Node-RED should:

1. Subscribe to command topics if those are used by web clients or other devices.
2. Maintain the authoritative counter value in flow/global context.
3. Clamp the value at zero.
4. Publish every accepted state change to `counters/capacity/state`.
5. Retain the state message if you want newly booted devices to immediately show the latest count.

## Retained Messages

For the state topic, retained messages are recommended:

```text
counters/capacity/state
```

A retained state message lets a freshly powered StopWatch or web dashboard immediately display the current counter value.

Battery topics can also be retained if the dashboard should show the last-known battery level while a device is offline.

## Multi-Device Synchronization

All StopWatch devices should subscribe to the same state topic:

```text
counters/capacity/state
```

When any device changes the count, Node-RED republishes the new authoritative value. Other devices receive that state update and refresh their local display.

## Future Multi-Counter Convention

For multiple counters, use a counter-name segment:

```text
counters/<counter_name>/state
counters/<counter_name>/increment
counters/<counter_name>/decrement
counters/<counter_name>/reset
```

Example:

```text
counters/front-door/state
counters/back-door/state
```
