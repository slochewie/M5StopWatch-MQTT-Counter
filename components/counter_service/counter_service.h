#pragma once

#include <cstdint>

namespace counter_service {

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
void setWifiEnabled(bool enabled, bool saveToSettings = true);
void setMqttEnabled(bool enabled, bool saveToSettings = true);

}  // namespace counter_service
