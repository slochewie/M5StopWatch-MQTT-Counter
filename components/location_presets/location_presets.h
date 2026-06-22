#pragma once

#include <cstddef>
#include <string>

namespace location_presets {

struct Preset {
    const char* id = "";
    const char* name = "";
    std::string wifi_ssid;
    std::string wifi_password;
    std::string mqtt_uri;
};

size_t count();
const char* nameAt(size_t index);
int selectedIndex();
const char* selectedName();
bool load(size_t index, Preset& preset);
bool apply(size_t index);

}  // namespace location_presets
