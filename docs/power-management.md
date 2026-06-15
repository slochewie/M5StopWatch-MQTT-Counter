# Power Management

Power management for the Counter app is under active development.

## Current Status

- Display/light-sleep helper code exists in the Counter app.
- IMU-orientation wake sampling exists for future wake behavior.
- Touch and physical button wake paths are planned.
- MQTT/network recovery is scheduled after wake.
- App-owned automatic display timeout is currently disabled while system-wide sleep management is being sorted out.

## Goals

- Reduce battery consumption during inactivity.
- Wake quickly from button, touch, or motion.
- Avoid false wakes while hanging from a lanyard.
- Keep MQTT state synchronized after wake.
- Preserve reliable button behavior.

## Current Wake Logic

The Counter app currently contains orientation wake logic based on accelerometer samples.

The intended wake condition is based on movement from the lanyard-hanging orientation toward the normal hand-held use orientation.

## Known Limitations

- Automatic sleep timeout is currently disabled.
- IMU wake behavior still needs real-world tuning.
- Network and MQTT reconnection after sleep may need more testing.
- Deeper PMIC sleep modes are still being evaluated.

## Future Work

- Move sleep behavior into a system-wide sleep manager.
- Tune IMU wake thresholds.
- Evaluate L1 sleep behavior with IMU wake.
- Improve MQTT recovery after wake.
- Add clear user-facing sleep/wake behavior documentation once finalized.
