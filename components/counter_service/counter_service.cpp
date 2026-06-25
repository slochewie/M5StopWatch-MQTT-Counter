#include "counter_service.h"

#include <device_config.h>
#include <hal/hal.h>
#include <apps/common/network/mqtt_service.h>
#include <apps/common/network/wifi_service.h>
#include <apps/common/sleep_manager/sleep_manager.h>
#include <hal/utils/settings/settings.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <sys/time.h>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

namespace counter_service {
namespace {

static constexpr const char* TAG = "CounterService";
static constexpr const char* TIME_TOPIC = "system/time/epoch";
static constexpr const char* LOCAL_TIMEZONE = "PST8PDT,M3.2.0,M11.1.0";
static constexpr const char* SETTINGS_NS = "counter_service";
static constexpr const char* WIFI_ENABLED_KEY = "wifi_enabled";
static constexpr const char* MQTT_ENABLED_KEY = "mqtt_enabled";
static constexpr const char* STARTUP_COUNTER_KEY = "startup_counter";
static constexpr time_t MIN_VALID_EPOCH = 1700000000;
static constexpr uint32_t BATTERY_SKIP_LOG_INTERVAL_MS = 30000;
static constexpr uint32_t BATTERY_PUBLISH_HEARTBEAT_MS = 300000;
static constexpr uint8_t BATTERY_UNKNOWN_PERCENT = 0xFF;

device_config::Config s_config;
std::string s_counter_topic;
std::string s_command_topic;
std::string s_battery_topic;
bool s_loaded = false;
bool s_started = false;
bool s_has_latest = false;
bool s_wifi_enabled = true;
bool s_mqtt_enabled = true;
StartupApp s_startup_app = StartupApp::Launcher;
int32_t s_latest_value = 0;
portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
uint32_t s_last_battery_skip_log_ms = 0;
uint32_t s_last_battery_publish_ms = 0;
uint8_t s_last_battery_publish_percent = BATTERY_UNKNOWN_PERCENT;

std::string replaceSuffix(const std::string& topic, const char* old_suffix, const char* new_suffix)
{
    const size_t len = std::strlen(old_suffix);
    if (topic.size() >= len && topic.compare(topic.size() - len, len, old_suffix) == 0) {
        return topic.substr(0, topic.size() - len) + new_suffix;
    }
    return topic;
}

std::string deriveCommandTopic(const std::string& state_topic)
{
    const auto replaced = replaceSuffix(state_topic, "/state", "/command");
    return replaced == state_topic ? state_topic + "/command" : replaced;
}

std::string deriveBatteryTopic(const std::string& state_topic)
{
    const char* device = s_config.device_name.empty() ? "m5stopwatch" : s_config.device_name.c_str();
    static constexpr const char* suffix = "/capacity/state";
    static constexpr size_t suffix_len = 15;

    if (state_topic.size() >= suffix_len &&
        state_topic.compare(state_topic.size() - suffix_len, suffix_len, suffix) == 0) {
        return state_topic.substr(0, state_topic.size() - suffix_len) + "/" + device + "/battery";
    }

    const auto replaced = replaceSuffix(state_topic, "/state", "/battery");
    return replaced == state_topic ? state_topic + "/battery" : replaced;
}

void applyLocalTimezone()
{
    GetHAL().setTimezone(LOCAL_TIMEZONE);
}

void applyNetworkPauseState()
{
    common::wifi::setRecoveryPaused(!s_wifi_enabled);
    common::mqtt::setRecoveryPaused(!s_wifi_enabled || !s_mqtt_enabled);
    if (!s_wifi_enabled || !s_mqtt_enabled) {
        s_started = false;
    }
}

void loadSettings()
{
    Settings settings(SETTINGS_NS, false);
    s_wifi_enabled = settings.GetBool(WIFI_ENABLED_KEY, true);
    s_mqtt_enabled = settings.GetBool(MQTT_ENABLED_KEY, true);
    s_startup_app = settings.GetBool(STARTUP_COUNTER_KEY, false) ? StartupApp::Counter : StartupApp::Launcher;
    applyNetworkPauseState();
}

void saveSettings()
{
    Settings settings(SETTINGS_NS, true);
    settings.SetBool(WIFI_ENABLED_KEY, s_wifi_enabled);
    settings.SetBool(MQTT_ENABLED_KEY, s_mqtt_enabled);
    settings.SetBool(STARTUP_COUNTER_KEY, s_startup_app == StartupApp::Counter);
}

void loadRuntimeConfig()
{
    applyLocalTimezone();
    s_config = device_config::load();
    loadSettings();

    const auto defaults = device_config::defaults();
    if (s_config.device_name.empty()) {
        s_config.device_name = defaults.device_name;
    }
    if (s_config.mqtt_uri.empty()) {
        s_config.mqtt_uri = defaults.mqtt_uri;
    }
    if (s_config.counter_topic.empty()) {
        s_config.counter_topic = defaults.counter_topic;
    }

    s_counter_topic = s_config.counter_topic;
    s_command_topic = deriveCommandTopic(s_counter_topic);
    s_battery_topic = deriveBatteryTopic(s_counter_topic);
    s_loaded = true;

    ESP_LOGI(TAG,
             "Loaded config: device=%s, broker=%s, state=%s, command=%s, battery=%s, ssid=%s, channel=%u, wifi=%s, mqtt=%s, startup=%s",
             s_config.device_name.c_str(),
             s_config.mqtt_uri.c_str(),
             s_counter_topic.c_str(),
             s_command_topic.c_str(),
             s_battery_topic.c_str(),
             s_config.wifi_ssid.empty() ? "<empty>" : s_config.wifi_ssid.c_str(),
             static_cast<unsigned>(s_config.wifi_channel),
             s_wifi_enabled ? "on" : "off",
             s_mqtt_enabled ? "on" : "off",
             s_startup_app == StartupApp::Counter ? "counter" : "launcher");
}

void setLatestValue(int32_t value)
{
    portENTER_CRITICAL(&s_lock);
    s_latest_value = value;
    s_has_latest = true;
    portEXIT_CRITICAL(&s_lock);
}

int32_t latestValueSnapshot()
{
    portENTER_CRITICAL(&s_lock);
    const int32_t value = s_latest_value;
    portEXIT_CRITICAL(&s_lock);
    return value;
}

bool parseCounterPayload(const char* payload, int32_t& value)
{
    if (payload == nullptr) {
        return false;
    }

    char* end = nullptr;
    long parsed = std::strtol(payload, &end, 10);
    if (end != payload) {
        value = static_cast<int32_t>(std::max<long>(parsed, 0));
        return true;
    }

    const char* key = std::strstr(payload, "value");
    if (key == nullptr) {
        return false;
    }

    const char* colon = std::strchr(key, ':');
    if (colon == nullptr) {
        return false;
    }

    parsed = std::strtol(colon + 1, &end, 10);
    if (end == colon + 1) {
        return false;
    }

    value = static_cast<int32_t>(std::max<long>(parsed, 0));
    return true;
}

bool parseEpochPayload(const char* payload, time_t& epoch)
{
    if (payload == nullptr) {
        return false;
    }

    char* end = nullptr;
    long long parsed = std::strtoll(payload, &end, 10);
    if (end != payload) {
        epoch = static_cast<time_t>(parsed);
        return epoch >= MIN_VALID_EPOCH;
    }

    const char* key = std::strstr(payload, "epoch");
    if (key == nullptr) {
        key = std::strstr(payload, "value");
    }
    if (key == nullptr) {
        return false;
    }

    const char* colon = std::strchr(key, ':');
    if (colon == nullptr) {
        return false;
    }

    parsed = std::strtoll(colon + 1, &end, 10);
    if (end == colon + 1) {
        return false;
    }

    epoch = static_cast<time_t>(parsed);
    return epoch >= MIN_VALID_EPOCH;
}

void handleCounterData(const char* payload)
{
    int32_t parsed = 0;
    if (!parseCounterPayload(payload, parsed)) {
        ESP_LOGW(TAG, "Ignoring unsupported counter payload: %s", payload == nullptr ? "<null>" : payload);
        return;
    }

    setLatestValue(parsed);
    ESP_LOGI(TAG, "Received %s = %ld", s_counter_topic.c_str(), static_cast<long>(parsed));
}

void handleTimeData(const char* payload)
{
    time_t epoch = 0;
    if (!parseEpochPayload(payload, epoch)) {
        ESP_LOGW(TAG, "Ignoring unsupported time payload: %s", payload == nullptr ? "<null>" : payload);
        return;
    }

    applyLocalTimezone();

    timeval tv = {
        .tv_sec = epoch,
        .tv_usec = 0,
    };

    if (settimeofday(&tv, nullptr) != 0) {
        ESP_LOGW(TAG, "settimeofday failed for epoch %lld", static_cast<long long>(epoch));
        return;
    }

    GetHAL().syncSystemTimeToRtc();

    std::tm local_tm = {};
    if (localtime_r(&epoch, &local_tm) != nullptr) {
        ESP_LOGI(TAG,
                 "System time synced from %s: epoch=%lld local=%04d-%02d-%02d %02d:%02d:%02d TZ=%s",
                 TIME_TOPIC,
                 static_cast<long long>(epoch),
                 local_tm.tm_year + 1900,
                 local_tm.tm_mon + 1,
                 local_tm.tm_mday,
                 local_tm.tm_hour,
                 local_tm.tm_min,
                 local_tm.tm_sec,
                 getenv("TZ") == nullptr ? "<unset>" : getenv("TZ"));
    }
}

void handleMqttMessage(const char* topic, const char* payload, void* user_data)
{
    if (topic == nullptr) {
        return;
    }

    if (s_counter_topic == topic) {
        handleCounterData(payload);
        return;
    }

    if (std::strcmp(topic, TIME_TOPIC) == 0) {
        handleTimeData(payload);
    }
}

void logBatteryPublishSkippedThrottled()
{
    const uint32_t now = GetHAL().millis();
    if (s_last_battery_skip_log_ms == 0 ||
        now - s_last_battery_skip_log_ms >= BATTERY_SKIP_LOG_INTERVAL_MS) {
        s_last_battery_skip_log_ms = now;
        ESP_LOGW(TAG, "Battery publish skipped, MQTT not ready");
    }
}

bool ensureMqttStarted()
{
    if (!s_loaded) {
        loadRuntimeConfig();
    }

    if (!s_wifi_enabled || !s_mqtt_enabled) {
        applyNetworkPauseState();
        return false;
    }

    if (!common::wifi::isConnected()) {
        return false;
    }

    if (common::mqtt::isStarted()) {
        common::mqtt::recoverConnection();
        return true;
    }

    common::mqtt::setRecoveryPaused(false);
    common::mqtt::subscribe(s_counter_topic.c_str(), 1, handleMqttMessage);
    common::mqtt::subscribe(TIME_TOPIC, 1, handleMqttMessage);

    common::mqtt::Config mqtt_config = {
        .uri = s_config.mqtt_uri,
        .client_id = s_config.device_name,
        .username = s_config.mqtt_username,
        .password = s_config.mqtt_password,
    };

    if (!common::mqtt::begin(mqtt_config)) {
        ESP_LOGW(TAG, "MQTT not started yet");
        return false;
    }

    s_started = true;
    ESP_LOGI(TAG, "Started");
    return true;
}

const char* actionForTargetValue(int32_t value, int32_t current)
{
    if (value == 0) {
        return "reset";
    }
    if (value == current + 1) {
        return "increment";
    }
    if (value == current - 1) {
        return "decrement";
    }
    return "set";
}

}  // namespace

void begin()
{
    if (!s_loaded) {
        loadRuntimeConfig();
    }

    applyNetworkPauseState();

    if (!s_wifi_enabled) {
        ESP_LOGI(TAG, "Wi-Fi disabled by Settings; network start skipped");
        return;
    }

    common::wifi::Config wifi_config = {
        .ssid = s_config.wifi_ssid,
        .password = s_config.wifi_password,
        .channel = s_config.wifi_channel,
    };

    if (!common::wifi::begin(wifi_config)) {
        ESP_LOGW(TAG, "Wi-Fi not connected yet; MQTT start deferred");
        return;
    }

    if (!s_mqtt_enabled) {
        ESP_LOGI(TAG, "MQTT disabled by Settings; MQTT start skipped");
        return;
    }

    (void)ensureMqttStarted();
}

void recoverConnection()
{
    if (!s_loaded) {
        loadRuntimeConfig();
    }

    if (sleep_manager::isSleeping()) {
        return;
    }

    applyNetworkPauseState();

    if (!s_wifi_enabled || common::wifi::isRecoveryPaused()) {
        return;
    }

    common::wifi::recoverConnection();

    if (!s_mqtt_enabled || !common::wifi::isConnected()) {
        return;
    }

    if (!common::mqtt::isStarted()) {
        begin();
        return;
    }

    common::mqtt::recoverConnection();
}

bool isStarted()
{
    return s_wifi_enabled && s_mqtt_enabled && (s_started || common::mqtt::isStarted());
}

bool isConnected()
{
    return s_wifi_enabled && s_mqtt_enabled && common::mqtt::isConnected();
}

bool publishValue(int32_t value)
{
    if (!s_wifi_enabled || !s_mqtt_enabled) {
        ESP_LOGI(TAG, "Publish skipped, network disabled by Settings");
        return false;
    }

    if (!ensureMqttStarted()) {
        ESP_LOGW(TAG, "Publish skipped, MQTT not ready");
        return false;
    }

    if (s_command_topic.empty()) {
        ESP_LOGW(TAG, "Publish skipped, command topic is empty");
        return false;
    }

    if (value < 0) {
        value = 0;
    }

    const int32_t current = latestValueSnapshot();
    const char* action = actionForTargetValue(value, current);

    char payload[192];
    if (std::strcmp(action, "set") == 0) {
        std::snprintf(payload,
                      sizeof(payload),
                      "{\"action\":\"set\",\"value\":%ld,\"source\":\"%s\"}",
                      static_cast<long>(value),
                      s_config.device_name.empty() ? "m5stopwatch" : s_config.device_name.c_str());
    } else {
        std::snprintf(payload,
                      sizeof(payload),
                      "{\"action\":\"%s\",\"source\":\"%s\"}",
                      action,
                      s_config.device_name.empty() ? "m5stopwatch" : s_config.device_name.c_str());
    }

    const bool ok = common::mqtt::publish(s_command_topic.c_str(), payload, 1, false);
    if (ok) {
        ESP_LOGI(TAG,
                 "Published command %s target=%ld current=%ld action=%s",
                 s_command_topic.c_str(),
                 static_cast<long>(value),
                 static_cast<long>(current),
                 action);
    }
    return ok;
}

bool publishBatteryPercentage(uint8_t percent)
{
    if (!s_wifi_enabled || !s_mqtt_enabled) {
        return false;
    }

    if (!common::wifi::isConnected() || !common::mqtt::isConnected()) {
        logBatteryPublishSkippedThrottled();
        return false;
    }

    if (s_battery_topic.empty()) {
        ESP_LOGW(TAG, "Battery publish skipped, topic is empty");
        return false;
    }

    if (percent > 100) {
        percent = 100;
    }

    const uint32_t now = GetHAL().millis();
    const bool percent_changed = s_last_battery_publish_percent == BATTERY_UNKNOWN_PERCENT ||
                                 percent != s_last_battery_publish_percent;
    const bool heartbeat_due = s_last_battery_publish_ms == 0 ||
                               now - s_last_battery_publish_ms >= BATTERY_PUBLISH_HEARTBEAT_MS;

    if (!percent_changed && !heartbeat_due) {
        return true;
    }

    char payload[128];
    std::snprintf(payload,
                  sizeof(payload),
                  "{\"battery\":%u,\"device\":\"%s\"}",
                  static_cast<unsigned>(percent),
                  s_config.device_name.empty() ? "m5stopwatch" : s_config.device_name.c_str());

    const bool ok = common::mqtt::publish(s_battery_topic.c_str(), payload, 1, true);
    if (ok) {
        s_last_battery_publish_ms = now;
        s_last_battery_publish_percent = percent;
        ESP_LOGI(TAG, "Published %s = %u", s_battery_topic.c_str(), static_cast<unsigned>(percent));
    }
    return ok;
}

bool takeLatestValue(int32_t& value)
{
    bool has_value = false;

    portENTER_CRITICAL(&s_lock);
    if (s_has_latest) {
        value = s_latest_value;
        s_has_latest = false;
        has_value = true;
    }
    portEXIT_CRITICAL(&s_lock);

    return has_value;
}

const char* statusText()
{
    if (!s_loaded) {
        loadRuntimeConfig();
    }

    if (!s_wifi_enabled) {
        return "WiFi Off";
    }

    if (!common::wifi::isConnected()) {
        common::wifi::recoverConnection();
        return common::wifi::statusText();
    }

    if (!s_mqtt_enabled) {
        return "MQTT Off";
    }

    if (!common::mqtt::isConnected()) {
        (void)ensureMqttStarted();
    }

    return common::mqtt::statusText();
}

const char* brokerUri()
{
    return s_config.mqtt_uri.empty() ? common::mqtt::brokerUri() : s_config.mqtt_uri.c_str();
}

const char* counterTopic()
{
    return s_counter_topic.empty() ? "" : s_counter_topic.c_str();
}

const char* batteryTopic()
{
    return s_battery_topic.empty() ? "" : s_battery_topic.c_str();
}

const char* deviceName()
{
    return s_config.device_name.empty() ? "" : s_config.device_name.c_str();
}

const char* wifiSsid()
{
    return s_config.wifi_ssid.empty() ? common::wifi::ssid() : s_config.wifi_ssid.c_str();
}

uint8_t wifiChannel()
{
    if (!s_loaded) {
        loadRuntimeConfig();
    }
    return s_config.wifi_channel;
}

bool isWifiEnabled()
{
    if (!s_loaded) {
        loadRuntimeConfig();
    }
    return s_wifi_enabled;
}

bool isMqttEnabled()
{
    if (!s_loaded) {
        loadRuntimeConfig();
    }
    return s_mqtt_enabled;
}

void setWifiEnabled(bool enabled, bool saveToSettings)
{
    if (!s_loaded) {
        loadRuntimeConfig();
    }

    if (s_wifi_enabled == enabled) {
        return;
    }

    s_wifi_enabled = enabled;
    if (!s_wifi_enabled) {
        s_started = false;
    }

    if (saveToSettings) {
        saveSettings();
    }

    applyNetworkPauseState();
    ESP_LOGI(TAG, "Wi-Fi setting changed: %s", s_wifi_enabled ? "on" : "off");

    if (s_wifi_enabled) {
        begin();
    }
}

void setWifiChannel(uint8_t channel, bool saveToSettings)
{
    if (!s_loaded) {
        loadRuntimeConfig();
    }

    if (!(channel == 0 || channel == 1 || channel == 6 || channel == 11)) {
        channel = 0;
    }

    if (s_config.wifi_channel == channel) {
        return;
    }

    s_config.wifi_channel = channel;
    s_started = false;

    if (saveToSettings) {
        device_config::save(s_config);
    }

    ESP_LOGI(TAG, "Wi-Fi channel setting changed: %u", static_cast<unsigned>(s_config.wifi_channel));
}

void setMqttEnabled(bool enabled, bool saveToSettings)
{
    if (!s_loaded) {
        loadRuntimeConfig();
    }

    if (s_mqtt_enabled == enabled) {
        return;
    }

    s_mqtt_enabled = enabled;
    if (!s_mqtt_enabled) {
        s_started = false;
    }

    if (saveToSettings) {
        saveSettings();
    }

    applyNetworkPauseState();
    ESP_LOGI(TAG, "MQTT setting changed: %s", s_mqtt_enabled ? "on" : "off");

    if (s_wifi_enabled && s_mqtt_enabled) {
        (void)ensureMqttStarted();
    }
}

StartupApp getStartupApp()
{
    if (!s_loaded) {
        loadRuntimeConfig();
    }
    return s_startup_app;
}

void setStartupApp(StartupApp app, bool saveToSettings)
{
    if (!s_loaded) {
        loadRuntimeConfig();
    }

    if (s_startup_app == app) {
        return;
    }

    s_startup_app = app;

    if (saveToSettings) {
        saveSettings();
    }

    ESP_LOGI(TAG, "Startup app setting changed: %s", startupAppName());
}

const char* startupAppName()
{
    if (!s_loaded) {
        loadRuntimeConfig();
    }
    return s_startup_app == StartupApp::Counter ? "Counter" : "Launcher";
}

}  // namespace counter_service
