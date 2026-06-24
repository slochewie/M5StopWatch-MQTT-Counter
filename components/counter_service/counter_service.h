#pragma once

#include <cstdint>

namespace counter_service {

enum class StartupApp : uint8_t {
    Launcher = 0,
    Counter  = 1,
};

void begin();
void recoverConnection();
bool isStarted();
bool isConnected();
bool publishValue(int32_t value);
bool publishBatteryPercentage(uint8_t percent);
bool takeLatestValue(int32_t& value);
const char* statusText();
const char* brokerUri();
const char* counterTopic();
const char* batteryTopic();
const char* deviceName();
const char* wifiSsid();

bool isWifiEnabled();
bool isMqttEnabled();
uint8_t wifiChannel();
void setWifiEnabled(bool enabled, bool saveToSettings = true);
void setMqttEnabled(bool enabled, bool saveToSettings = true);
void setWifiChannel(uint8_t channel, bool saveToSettings = true);

StartupApp getStartupApp();
void setStartupApp(StartupApp app, bool saveToSettings = true);
const char* startupAppName();

}  // namespace counter_service
