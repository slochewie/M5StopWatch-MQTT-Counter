# Power Management

Power management is active development. The project now has a system-level sleep manager that owns the main idle behavior instead of the Counter app owning an app-local timeout.

## Current Implementation

The sleep manager tracks user activity, device orientation, display standby, network pause/resume, and the ESP32-S3 deep-sleep target.

Current thresholds:

| Idle condition | Behavior |
| --- | --- |
| 10 seconds idle while hanging | Display/network standby. |
| 30 seconds idle while hanging | ESP32-S3 native deep sleep target. |

Both thresholds require the device to be in the lanyard-hanging orientation. If the device is not hanging, the sleep manager treats that as active/handheld use and keeps the display/network awake.

## Hanging Orientation Gate

The sleep manager uses IMU samples to avoid sleeping while the device is being held.

Current hanging check:

- Y acceleration must be strongly positive.
- Z acceleration must be near zero.
- The condition must be confirmed across multiple samples.

This matches the intended physical behavior: the StopWatch may hang vertically from a lanyard when idle, but should remain awake during normal handheld use.

## 10-Second Display/Network Standby

After the standby threshold is reached while hanging, the firmware:

- Saves the current brightness.
- Sets the backlight brightness to zero.
- Puts the display to sleep.
- Pauses MQTT recovery.
- Pauses Wi-Fi recovery.
- Disconnects/stops Wi-Fi.

Standby wake behavior:

- Touch wakes display/network standby.
- Physical button activity wakes display/network standby.
- Motion/orientation alone does not wake the 10-second standby state.

Network recovery remains deferred until needed after wake.

## 30-Second ESP32-S3 Deep Sleep

After the deeper timeout is reached while hanging, the firmware enters native ESP32-S3 deep sleep.

Current deep-sleep target:

- Turns display/backlight off.
- Pauses/stops network activity.
- Configures EXT0 wake from touch interrupt.
- Uses GPIO13 / `G13_TP_INT`.
- Wake level is low.

Deep sleep wake behaves like an ESP32 deep-sleep boot path. The firmware logs the wake cause on startup.

## Network Resume

Network resume is intentionally deferred.

When the Counter app needs to publish a count, it requests network resume from the sleep manager. The sleep manager restarts Wi-Fi recovery and MQTT recovery, and the Counter app retries any pending publish.

This avoids immediately bringing the radio back up for every wake if no publish is needed.

## Counter App Interaction

The Counter app no longer owns its normal automatic display timeout. Its local timeout constant is disabled so the system sleep manager can own standby/deep-sleep behavior.

The Counter app still:

- Avoids normal foreground work while the sleep manager reports sleeping.
- Avoids unnecessary MQTT sync attempts during sleep.
- Keeps a pending publish value if a publish was requested while the network was unavailable.
- Requests network resume when it needs to publish.

## AMOLED / Display Efficiency

The Counter app UI uses a true-black LVGL panel background, which is appropriate for the StopWatch AMOLED display.

Additional power-saving behavior comes from:

- Backlight/display sleep during standby.
- Pausing Wi-Fi/MQTT recovery during sleep states.
- Deferring radio recovery until needed.

## Known Limitations

- Deep sleep currently uses touch wake as the explicit ESP32 wake source.
- Motion/PMG0/BMI270 wake work exists in the runtime sleep-manager code, but deep sleep still enters the ESP32-S3 native deep-sleep path.
- IMU sleep/wake thresholds may need real-world tuning.
- Wake and network-recovery behavior still needs longer battery-runtime testing.
- PMIC L1/L0 paths are still being evaluated separately from the current L2/ESP32 deep-sleep target.

## Future Work

- Continue tuning hanging and handheld orientation thresholds.
- Improve wake behavior for real venue/lanyard use.
- Validate battery runtime in standby and deep sleep.
- Evaluate deeper PMIC-assisted sleep modes after L2 behavior is stable.
- Document final user-facing sleep/wake behavior after field testing.
