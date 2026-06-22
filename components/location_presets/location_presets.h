#pragma once

#include <cstddef>

namespace location_presets {

struct Preset {
    const char* id;
    const char* name;
    const char* wifi_ssid;
    const char* wifi_password;
    const char* mqtt_uri;
};

size_t count();
const Preset* at(size_t index);
int selectedIndex();
const char* selectedName();
bool apply(size_t index);

}  // namespace location_presets
