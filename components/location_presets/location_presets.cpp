#include "location_presets.h"

#include <device_config.h>
#include <esp_err.h>
#include <esp_log.h>
#include <nvs.h>
#include <nvs_flash.h>

namespace location_presets {

// This function is provided by the generated, gitignored file:
// components/location_presets/location_presets_generated.cpp
//
// Generate it with:
//   python tools/generate_location_presets.py
//
// Intentionally not weak: if the generated file is missing from the build,
// the build/link should fail instead of producing runtime "preset unavailable"
// warnings that hide the real problem.
const Preset* generatedPresets(size_t* count);

namespace {

constexpr const char* TAG = "LocationPresets";
constexpr const char* STATE_NS = "location_state";
constexpr const char* SELECTED_KEY = "selected";

bool ensureNvsReady()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS init requested erase: %s", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

const Preset* presets(size_t* out_count)
{
    return generatedPresets(out_count);
}

void saveSelectedIndex(int index)
{
    if (!ensureNvsReady()) {
        return;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(STATE_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open state failed: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_set_i32(handle, SELECTED_KEY, index);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_set_i32 selected failed: %s", esp_err_to_name(err));
        nvs_close(handle);
        return;
    }

    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_commit selected failed: %s", esp_err_to_name(err));
    }

    nvs_close(handle);
}

}  // namespace

size_t count()
{
    size_t preset_count = 0;
    (void)presets(&preset_count);
    return preset_count;
}

const Preset* at(size_t index)
{
    size_t preset_count = 0;
    const Preset* preset_list = presets(&preset_count);
    if (preset_list == nullptr || index >= preset_count) {
        return nullptr;
    }
    return &preset_list[index];
}

int selectedIndex()
{
    if (!ensureNvsReady()) {
        return -1;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(STATE_NS, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return -1;
    }

    int32_t selected = -1;
    err = nvs_get_i32(handle, SELECTED_KEY, &selected);
    nvs_close(handle);

    if (err != ESP_OK || selected < 0 || selected >= static_cast<int32_t>(count())) {
        return -1;
    }
    return static_cast<int>(selected);
}

const char* selectedName()
{
    const int index = selectedIndex();
    const Preset* preset = index < 0 ? nullptr : at(static_cast<size_t>(index));
    return preset == nullptr ? "Manual" : preset->name;
}

bool apply(size_t index)
{
    const Preset* preset = at(index);
    if (preset == nullptr) {
        ESP_LOGW(TAG, "Location preset %u unavailable", static_cast<unsigned>(index));
        return false;
    }

    if (preset->wifi_ssid == nullptr || preset->wifi_ssid[0] == '\0' ||
        preset->mqtt_uri == nullptr || preset->mqtt_uri[0] == '\0') {
        ESP_LOGW(TAG, "Location preset %s is incomplete", preset->name == nullptr ? "<unnamed>" : preset->name);
        return false;
    }

    device_config::Config config = device_config::load();
    config.wifi_ssid = preset->wifi_ssid;
    config.wifi_password = preset->wifi_password == nullptr ? "" : preset->wifi_password;
    config.mqtt_uri = preset->mqtt_uri;
    config.wifi_channel = 0;  // Location selection defaults WiFi channel back to Auto.

    if (!device_config::save(config)) {
        ESP_LOGE(TAG, "Failed to save active config for %s", preset->name == nullptr ? "<unnamed>" : preset->name);
        return false;
    }

    saveSelectedIndex(static_cast<int>(index));
    ESP_LOGI(TAG, "Applied location preset: %s", preset->name == nullptr ? "<unnamed>" : preset->name);
    return true;
}

}  // namespace location_presets
