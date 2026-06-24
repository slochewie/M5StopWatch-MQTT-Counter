#pragma once

#include <cstdint>
#include <string>

namespace common::wifi {

struct Config {
    std::string ssid;
    std::string password;
    uint8_t channel = 0;  // 0 = auto/all channels, 1/6/11 = preferred 2.4 GHz channel
};

bool begin(const Config& config);
void recoverConnection();
void setRecoveryPaused(bool paused);
bool isRecoveryPaused();
bool isStarted();
bool isConnected();
const char* ssid();
uint8_t channel();
const char* statusText();

}  // namespace common::wifi
