#include "location_presets.h"

#include <device_config.h>
#include <esp_err.h>
#include <esp_log.h>
#include <nvs.h>
#include <nvs_flash.h>

#include <cstdio>

namespace location_presets {
namespace {

constexpr const char* TAG = "LocationPresets";
constexpr const char* PRESET_PARTITION = "preset_nvs";
constexpr const char* PRESET_NS = "location_presets";
constexpr const char* STATE_NS = "location_state";
constexpr const char* SELECTED_KEY = "selected";

struct LocationMeta {
    const char* id;
    const char* name;
};

constexpr LocationMeta kLocations[] = {
    {"home", "Home"},
    {"mccarthys", "McCarthy's"},
    {"library", "Library"},
    {"frog_peach", "Frog & Peach"},
    {"bulls", "Bull's"},
};

bool ensureDefaultNvsReady()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Default NVS init requested erase: %s", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool ensurePresetNvsReady()
{
    esp_err_t err = nvs_flash_init_partition(PRESET_PARTITION);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Preset NVS init requested erase: %s", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase_partition(PRESET_PARTITION));
        err = nvs_flash_init_partition(PRESET_PARTITION);
    }
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "preset_nvs unavailable: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

std::string readString(nvs_handle_t handle, const char* key)
{
    size_t length = 0;
    esp_err_t err = nvs_get_str(handle, key, nullptr, &length);
    if (err == ESP_ERR_NVS_NOT_FOUND || length == 0) {
        return {};
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read %s: %s", key, esp_err_to_name(err));
        return {};
    }

    std::string value(length, '\0');
    err = nvs_get_str(handle, key, value.data(), &length);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read %s value: %s", key, esp_err_to_name(err));
        return {};
    }
    if (!value.empty() && value.back() == '\0') {
        value.pop_back();
    }
    return value;
}

bool readPresetValue(nvs_handle_t handle, const char* id, const char* suffix, std::string& value)
{
    char key[32];
    std::snprintf(key, sizeof(key), "%s_%s", id, suffix);
    value = readString(handle, key);
    return !value.empty();
}

void saveSelectedIndex(int index)
{
    if (!ensureDefaultNvsReady()) {
        return;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(STATE_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open state failed: %s", esp_err_to_name(err));
        return;
    }

    nvs_set_i32(handle, SELECTED_KEY, index);
    nvs_commit(handle);
    nvs_close(handle);
}

}  // namespace

size_t count()
{
    return sizeof(kLocations) / sizeof(kLocations[0]);
}

const char* nameAt(size_t index)
{
    if (index >= count()) {
        return "";
    }
    return kLocations[index].name;
}

int selectedIndex()
{
    if (!ensureDefaultNvsReady()) {
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
    return index < 0 ? "Manual" : nameAt(static_cast<size_t>(index));
}

bool load(size_t index, Preset& preset)
{
    if (index >= count()) {
        return false;
    }
    if (!ensurePresetNvsReady()) {
        return false;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open_from_partition(PRESET_PARTITION, PRESET_NS, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open_from_partition failed: %s", esp_err_to_name(err));
        return false;
    }

    preset.id = kLocations[index].id;
    preset.name = kLocations[index].name;

    bool ok = true;
    ok = readPresetValue(handle, preset.id, "wifi_ssid", preset.wifi_ssid) && ok;
    ok = readPresetValue(handle, preset.id, "wifi_pass", preset.wifi_password) && ok;
    ok = readPresetValue(handle, preset.id, "mqtt_uri", preset.mqtt_uri) && ok;

    nvs_close(handle);

    if (!ok) {
        ESP_LOGW(TAG, "Location preset %s is incomplete", preset.name);
    }
    return ok;
}

bool apply(size_t index)
{
    Preset preset;
    if (!load(index, preset)) {
        return false;
    }

    device_config::Config config = device_config::load();
    config.wifi_ssid = preset.wifi_ssid;
    config.wifi_password = preset.wifi_password;
    config.mqtt_uri = preset.mqtt_uri;

    if (!device_config::save(config)) {
        ESP_LOGE(TAG, "Failed to save active config for %s", preset.name);
        return false;
    }

    saveSelectedIndex(static_cast<int>(index));
    ESP_LOGI(TAG, "Applied location preset: %s", preset.name);
    return true;
}

}  // namespace location_presets
