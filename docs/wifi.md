Wi-Fi Architecture

Overview

The M5StopWatch MQTT Counter uses a dedicated networking subsystem located in main/apps/common/network, with wifi_service serving as the central manager for wireless connectivity.

Rather than operating as a simple background task, wifi_service is tightly integrated with the firmware’s power management, MQTT infrastructure, and runtime settings. This allows the StopWatch to maintain reliable network connectivity while minimizing battery consumption during idle periods.

wifi_service

wifi_service is responsible for:

* Initializing the ESP32-S3 Wi-Fi stack.
* Connecting to the configured wireless network.
* Monitoring and reporting connection status.
* Automatically reconnecting after temporary outages.
* Coordinating with the system sleep manager.
* Providing the network transport used by mqtt_service and other network-aware components.

The Counter app and Settings app do not communicate directly with the ESP-IDF Wi-Fi APIs. Instead, they rely on wifi_service to manage the underlying connection lifecycle.

Access Point (AP) Portal

One of the primary responsibilities of wifi_service is supporting the built-in Access Point (AP) Portal.

If valid Wi-Fi credentials are unavailable or the StopWatch cannot successfully join the configured network, the firmware can start a temporary wireless access point. Users connect directly to this AP to configure network settings through the provisioning interface.

Typical uses include:

* First-time device setup.
* Updating Wi-Fi credentials after a network change.
* Recovering devices that can no longer join the configured infrastructure.
* Deploying devices without requiring firmware recompilation or USB access.

Once valid credentials are saved, the AP Portal is exited and the device returns to normal client mode.

Relationship to mqtt_service

mqtt_service builds on top of wifi_service and depends on it for all network communication.

The normal startup sequence is:

1. wifi_service initializes the Wi-Fi subsystem.
2. The StopWatch connects to the configured wireless network.
3. mqtt_service establishes a connection to the configured MQTT broker.
4. Counter state, battery telemetry, and optional time synchronization become active.

If Wi-Fi is unavailable, mqtt_service automatically waits until network connectivity has been restored before attempting reconnection.

Sleep and Power Management

The Wi-Fi subsystem is designed to cooperate closely with the firmware’s sleep manager to maximize battery life.

During display/network standby:

* The AMOLED display is powered down.
* Wi-Fi activity is suspended.
* MQTT reconnection attempts are paused.
* Unnecessary background network traffic is avoided.
* Network recovery is deferred until the device resumes active operation.

This behavior significantly reduces idle power consumption while preventing repeated reconnect attempts during intentional sleep periods.

Wake Behavior

When the StopWatch wakes from touch input, button interaction, or another supported wake source:

1. The firmware resumes normal execution.
2. wifi_service restores or reconnects the Wi-Fi interface as needed.
3. Deferred network recovery resumes.
4. mqtt_service reconnects to the configured broker.
5. The latest retained MQTT state is synchronized, ensuring the displayed counter matches the authoritative value.

This design allows devices to wake quickly while remaining synchronized with the rest of the system.

Runtime Settings

The Settings application provides user control over networking features.

Users can enable or disable Wi-Fi without recompiling firmware. Disabling Wi-Fi also disables MQTT connectivity until networking is re-enabled.

This allows the StopWatch to operate entirely offline when required.

Design Goals

The networking architecture was designed with the following objectives:

* Reliable wireless connectivity.
* Seamless integration with MQTT synchronization.
* Automatic recovery after temporary network outages.
* Efficient battery usage during idle periods.
* User-friendly provisioning through the AP Portal.
* Clear separation between application logic and networking infrastructure.

By centralizing all wireless functionality within wifi_service, the firmware keeps networking concerns isolated from application code while providing a consistent foundation for current and future features.