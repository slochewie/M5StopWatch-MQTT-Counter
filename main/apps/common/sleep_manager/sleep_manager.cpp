/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "sleep_manager.h"

#include "esp_sleep.h"
#include <hal/hal.h>
#include <apps/common/network/wifi_service.h>
#include <apps/common/network/mqtt_service.h>
#include <mooncake_log.h>
#include <cmath>
#include <esp_wifi.h>
#include <esp_err.h>

#include <driver/gpio.h>
#include <driver/rtc_io.h>

#ifndef USE_PMIC_SLEEP
#define USE_PMIC_SLEEP 0
#endif

#ifndef SLEEP_MANAGER_TOUCH_WAKE_GPIO
#define SLEEP_MANAGER_TOUCH_WAKE_GPIO 13
#endif

#ifndef SLEEP_MANAGER_TOUCH_WAKE_LEVEL
#define SLEEP_MANAGER_TOUCH_WAKE_LEVEL 0
#endif

#ifndef SLEEP_MANAGER_KEEP_TIMER_WAKE
#define SLEEP_MANAGER_KEEP_TIMER_WAKE 0
#endif

namespace sleep_manager {
namespace {

static constexpr const char* TAG = "SleepManager";

static constexpr uint32_t DISPLAY_STANDBY_TIMEOUT_MS = 10000;
static constexpr uint32_t DEEP_SLEEP_TIMEOUT_MS = 30000;
static constexpr uint32_t IMU_SAMPLE_INTERVAL_MS = 100;
static constexpr uint32_t POST_SLEEP_WAKE_LOCKOUT_MS = 1200;
static constexpr uint32_t PMG0_SAMPLE_INTERVAL_MS = 100;
static constexpr uint8_t BMI270_ANY_MOTION_STATUS_MASK = 0x40;

static constexpr float HANGING_Y_MIN = 0.70f;
static constexpr float HANGING_Z_ABS_MAX = 0.45f;

static constexpr float WAKE_Y_MAX = -0.20f;
static constexpr float WAKE_Z_MIN = 0.75f;

static constexpr uint8_t SLEEP_CONFIRM_SAMPLES = 8;
static constexpr uint8_t WAKE_CONFIRM_SAMPLES = 3;
static constexpr uint8_t PMG0_WAKE_CONFIRM_SAMPLES = 1;

bool s_initialized = false;
bool s_inhibit = false;
bool s_sleeping = false;
bool s_display_standby = false;

uint32_t s_last_activity_ms = 0;
uint32_t s_last_imu_sample_ms = 0;
uint32_t s_sleep_entered_ms = 0;

int s_saved_brightness = 80;

uint8_t s_sleep_orientation_count = 0;
uint8_t s_wake_orientation_count = 0;

bool s_last_pmg0_level_valid = false;
uint8_t s_last_pmg0_level = 0;
uint32_t s_last_pmg0_sample_ms = 0;
uint8_t s_pmg0_wake_confirm_count = 0;
bool s_motion_wake_candidate = false;
uint32_t s_motion_wake_candidate_ms = 0;

void resetPmg0Sampler()
{
    s_last_pmg0_level_valid = false;
    s_last_pmg0_level = 0;
    s_last_pmg0_sample_ms = 0;
    s_pmg0_wake_confirm_count = 0;
    s_motion_wake_candidate = false;
    s_motion_wake_candidate_ms = 0;
}

bool samplePmg0IfDue(const char* reason)
{
    const uint32_t now = GetHAL().millis();
    if (s_last_pmg0_sample_ms != 0 && now - s_last_pmg0_sample_ms < PMG0_SAMPLE_INTERVAL_MS) {
        return false;
    }
    s_last_pmg0_sample_ms = now;

    uint8_t level = 0;
    if (!GetHAL().pmicGetPmg0Level(level)) {
        return false;
    }

    if (!s_last_pmg0_level_valid) {
        s_last_pmg0_level_valid = true;
        s_last_pmg0_level = level;
        return false;
    }

    if (level == s_last_pmg0_level) {
        return false;
    }

    uint8_t imu_status = 0;
    const bool have_imu_status = GetHAL().imuGetInterruptStatus0(imu_status);

    const uint8_t previous_level = s_last_pmg0_level;
    s_last_pmg0_level = level;

    const bool rising_edge = previous_level == 0 && level == 1;
    const bool bmi270_any_motion = have_imu_status && ((imu_status & BMI270_ANY_MOTION_STATUS_MASK) != 0);

    if (rising_edge && bmi270_any_motion) {
        mclog::tagInfo(TAG,
                       "PMG0/BMI270 any-motion edge: {} -> {}, INT_STATUS_0=0x{:02X}",
                       static_cast<int>(previous_level),
                       static_cast<int>(level),
                       static_cast<int>(imu_status));
        if (s_pmg0_wake_confirm_count < PMG0_WAKE_CONFIRM_SAMPLES) {
            ++s_pmg0_wake_confirm_count;
        }

        mclog::tagInfo(TAG,
                       "PMG0/BMI270 wake candidate {}/{}",
                       static_cast<int>(s_pmg0_wake_confirm_count),
                       static_cast<int>(PMG0_WAKE_CONFIRM_SAMPLES));

        return s_pmg0_wake_confirm_count >= PMG0_WAKE_CONFIRM_SAMPLES;
    }

    if (!bmi270_any_motion) {
        s_pmg0_wake_confirm_count = 0;
    }

    return false;
}

bool readButtonActivity()
{
    GetHAL().updateButtonStates();

    return GetHAL().btnA.wasClicked() ||
           GetHAL().btnB.wasClicked() ||
           GetHAL().btnPwr.wasClicked();
}

bool readTouchActivity()
{
    return GetHAL().getTouchPoint().num > 0;
}

bool sampleImuIfDue()
{
    const uint32_t now = GetHAL().millis();
    if (s_last_imu_sample_ms != 0 && now - s_last_imu_sample_ms < IMU_SAMPLE_INTERVAL_MS) {
        return false;
    }

    s_last_imu_sample_ms = now;
    GetHAL().updateImuData();
    return true;
}

bool isHangingOrientation()
{
    const auto& imu = GetHAL().getImuData();

    return imu.accelY >= HANGING_Y_MIN &&
           std::fabs(imu.accelZ) <= HANGING_Z_ABS_MAX;
}

bool isWakeOrientation()
{
    const auto& imu = GetHAL().getImuData();

    return imu.accelY <= WAKE_Y_MAX &&
           imu.accelZ >= WAKE_Z_MIN;
}

void armMotionWakeCandidate(const char* reason)
{
    s_motion_wake_candidate = true;
    s_motion_wake_candidate_ms = GetHAL().millis();
    s_wake_orientation_count = 0;

    mclog::tagInfo(TAG,
                   "PMG0/BMI270 motion candidate armed: {}",
                   reason ? reason : "unknown");
}

void disconnectNetworkForSleep()
{
    mclog::tagInfo(TAG, "network sleep: pause MQTT/Wi-Fi and stop radio");

    common::mqtt::setRecoveryPaused(true);
    common::wifi::setRecoveryPaused(true);

    const esp_err_t disconnect_err = esp_wifi_disconnect();
    if (disconnect_err != ESP_OK &&
        disconnect_err != ESP_ERR_WIFI_NOT_INIT &&
        disconnect_err != ESP_ERR_WIFI_NOT_STARTED &&
        disconnect_err != ESP_ERR_WIFI_CONN) {
        mclog::tagWarn(TAG, "esp_wifi_disconnect failed: {}", esp_err_to_name(disconnect_err));
    }

    const esp_err_t stop_err = esp_wifi_stop();
    if (stop_err != ESP_OK &&
        stop_err != ESP_ERR_WIFI_NOT_INIT &&
        stop_err != ESP_ERR_WIFI_NOT_STARTED) {
        mclog::tagWarn(TAG, "esp_wifi_stop failed: {}", esp_err_to_name(stop_err));
    }
}

void enforceNetworkPauseWhileSleeping()
{
    if (!common::mqtt::isRecoveryPaused()) {
        mclog::tagWarn(TAG, "MQTT resumed while sleeping; re-pausing");
        common::mqtt::setRecoveryPaused(true);
    }

    if (!common::wifi::isRecoveryPaused()) {
        mclog::tagWarn(TAG, "Wi-Fi resumed while sleeping; re-pausing");
        common::wifi::setRecoveryPaused(true);
    }
}

void enterDisplayStandby()
{
    if (s_display_standby) {
        return;
    }

    s_display_standby = true;
    s_saved_brightness = GetHAL().getBackLightBrightness();
    if (s_saved_brightness <= 0) {
        s_saved_brightness = 20;
    }

    mclog::tagInfo(TAG, "display/network standby enter after {} ms idle",
                   DISPLAY_STANDBY_TIMEOUT_MS);

    GetHAL().setBackLightBrightness(0);
    GetHAL().getDisplay().sleep();

    disconnectNetworkForSleep();
}

void exitDisplayStandby()
{
    if (!s_display_standby) {
        return;
    }

    mclog::tagInfo(TAG, "display standby wake; network remains deferred");
    s_display_standby = false;

    GetHAL().getDisplay().wakeup();
    GetHAL().setBackLightBrightness(s_saved_brightness > 0 ? s_saved_brightness : 20);

    resetPmg0Sampler();
}

void restoreNetworkAfterWake()
{
    if (!s_sleeping) {
        mclog::tagWarn(TAG, "restoreNetworkAfterWake() ignored because sleep manager is not sleeping");
        return;
    }

    mclog::tagInfo(TAG, "network wake deferred until requested");
}

void enterSleep()
{
    if (s_sleeping) {
        return;
    }

    mclog::tagInfo(TAG, "ESP32 deep sleep enter");

    s_display_standby = false;
    GetHAL().setBackLightBrightness(0);
    GetHAL().getDisplay().sleep();

    disconnectNetworkForSleep();

    // L2 target: ESP32-S3 native deep sleep, wake from CST820 touch interrupt.
    // Schematic: G13_TP_INT is ESP32 GPIO13. TP_INT is normally pulled high and
    // asserts low, so EXT0 wakes on level 0.
    mclog::tagInfo(TAG,
                   "deep sleep wake: EXT0 touch GPIO{} level={}",
                   SLEEP_MANAGER_TOUCH_WAKE_GPIO,
                   SLEEP_MANAGER_TOUCH_WAKE_LEVEL);

    const gpio_num_t touch_wake_gpio = static_cast<gpio_num_t>(SLEEP_MANAGER_TOUCH_WAKE_GPIO);
    ESP_ERROR_CHECK(rtc_gpio_pullup_en(touch_wake_gpio));
    ESP_ERROR_CHECK(rtc_gpio_pulldown_dis(touch_wake_gpio));
    ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(touch_wake_gpio,
                                                 SLEEP_MANAGER_TOUCH_WAKE_LEVEL));

#if SLEEP_MANAGER_KEEP_TIMER_WAKE
    mclog::tagWarn(TAG, "deep sleep wake: timer safety net enabled for 10 seconds");
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(10ULL * 1000000ULL));
#endif

    esp_deep_sleep_start();
}

void exitSleep()
{
    if (!s_sleeping) {
        return;
    }

    s_sleep_orientation_count = 0;
    s_wake_orientation_count = 0;
    s_last_activity_ms = GetHAL().millis();

    mclog::tagInfo(TAG, "display sleep wake");
    GetHAL().pmicExitAppSleep();

    GetHAL().getDisplay().wakeup();
    GetHAL().setBackLightBrightness(s_saved_brightness > 0 ? s_saved_brightness : 80);

    restoreNetworkAfterWake();

    s_sleeping = false;
    s_display_standby = false;
    resetPmg0Sampler();
}

void resetIdleState()
{
    s_last_activity_ms = GetHAL().millis();
    s_sleep_orientation_count = 0;
    s_wake_orientation_count = 0;
    s_motion_wake_candidate = false;
    s_motion_wake_candidate_ms = 0;
}

}  // namespace

void begin()
{
    if (s_initialized) {
        return;
    }

    s_initialized = true;
    const esp_sleep_wakeup_cause_t wake_cause = esp_sleep_get_wakeup_cause();
    switch (wake_cause) {
        case ESP_SLEEP_WAKEUP_TIMER:
            mclog::tagInfo(TAG, "ESP32 wake cause: timer");
            break;
        case ESP_SLEEP_WAKEUP_EXT0:
            mclog::tagInfo(TAG,
                           "ESP32 wake cause: EXT0 touch GPIO{}",
                           SLEEP_MANAGER_TOUCH_WAKE_GPIO);
            break;
        case ESP_SLEEP_WAKEUP_EXT1:
            mclog::tagInfo(TAG, "ESP32 wake cause: EXT1");
            break;
        case ESP_SLEEP_WAKEUP_GPIO:
            mclog::tagInfo(TAG, "ESP32 wake cause: GPIO");
            break;
        case ESP_SLEEP_WAKEUP_UNDEFINED:
            mclog::tagInfo(TAG, "ESP32 wake cause: normal boot/reset");
            break;
        default:
            mclog::tagInfo(TAG,
                           "ESP32 wake cause: {}",
                           static_cast<int>(wake_cause));
            break;
    }
    resetIdleState();
    mclog::tagInfo(TAG, "begin");
}

void update()
{
    if (!s_initialized) {
        begin();
    }

    const uint32_t now = GetHAL().millis();

    if (s_inhibit) {
        if (s_sleeping) {
            exitSleep();
        }
        if (s_display_standby) {
            exitDisplayStandby();
        }
        resetIdleState();
        return;
    }

    if (s_sleeping) {
        enforceNetworkPauseWhileSleeping();

        const bool button_activity = readButtonActivity();
        const bool touch_activity = readTouchActivity();

        if (button_activity) {
            mclog::tagInfo(TAG, "button wake");
            exitSleep();
            return;
        }

        if (touch_activity) {
            mclog::tagInfo(TAG, "touch ignored while sleeping");
        }

        if (now - s_sleep_entered_ms < POST_SLEEP_WAKE_LOCKOUT_MS) {
            (void)samplePmg0IfDue("sleep-lockout");
            return;
        }

        if (samplePmg0IfDue("sleep-loop")) {
            armMotionWakeCandidate("sleep-loop");
        }

        if (sampleImuIfDue()) {
            if (s_motion_wake_candidate && isWakeOrientation()) {
                if (s_wake_orientation_count < WAKE_CONFIRM_SAMPLES) {
                    ++s_wake_orientation_count;
                }

                mclog::tagInfo(TAG,
                               "orientation wake candidate {}/{}",
                               static_cast<int>(s_wake_orientation_count),
                               static_cast<int>(WAKE_CONFIRM_SAMPLES));
            } else {
                if (s_motion_wake_candidate && !isWakeOrientation()) {
                    const auto& imu = GetHAL().getImuData();
                    mclog::tagInfo(TAG,
                                   "motion ignored; not handheld orientation: y={:.2f} z={:.2f}",
                                   imu.accelY,
                                   imu.accelZ);
                }
                s_wake_orientation_count = 0;
            }

            if (s_motion_wake_candidate && s_wake_orientation_count >= WAKE_CONFIRM_SAMPLES) {
                mclog::tagInfo(TAG, "PMG0/BMI270 orientation-confirmed wake");
                exitSleep();
                return;
            }
        }

        return;
    }

    const bool touch_activity = readTouchActivity();
    const bool button_activity = GetHAL().btnA.isPressed() ||
                                 GetHAL().btnB.isPressed() ||
                                 GetHAL().btnPwr.isPressed();

    if (s_display_standby && (touch_activity || button_activity)) {
        exitDisplayStandby();
        markActivity();
        return;
    }

    if (!s_display_standby && touch_activity) {
        markActivity();
        return;
    }

    if (!s_display_standby && button_activity) {
        markActivity();
        return;
    }

    const uint32_t idle_ms = now - s_last_activity_ms;

    // Both the 10-second display/network standby and the 30-second L2 deep sleep
    // are allowed only while the StopWatch is hanging upside down. If it is not
    // hanging, treat that as active/handheld use and keep the display/network awake.
    if (!sampleImuIfDue()) {
        return;
    }

    const bool hanging = isHangingOrientation();

    static uint32_t s_last_sleep_diag_ms = 0;
    if (now - s_last_sleep_diag_ms >= 2000) {
        s_last_sleep_diag_ms = now;
        mclog::tagInfo(TAG,
                       "sleep check: idle={} hanging={} confirm={}",
                       idle_ms,
                       hanging ? 1 : 0,
                       static_cast<int>(s_sleep_orientation_count));
    }

    if (!hanging) {
        s_sleep_orientation_count = 0;

        // Do not wake the 10-second display/network standby just because the
        // device moved out of hanging orientation. Standby wake is intentionally
        // touch/button only; motion/orientation should only prevent entering
        // deeper L2 sleep.
        if (!s_display_standby) {
            s_last_activity_ms = now;
        }
        return;
    }

    if (s_sleep_orientation_count < SLEEP_CONFIRM_SAMPLES) {
        ++s_sleep_orientation_count;
    }

    if (!s_display_standby &&
        idle_ms >= DISPLAY_STANDBY_TIMEOUT_MS &&
        s_sleep_orientation_count >= SLEEP_CONFIRM_SAMPLES) {
        enterDisplayStandby();
    }

    if (idle_ms >= DEEP_SLEEP_TIMEOUT_MS &&
        s_sleep_orientation_count >= SLEEP_CONFIRM_SAMPLES) {
        enterSleep();
    }
}

void setInhibit(bool inhibit)
{
    s_inhibit = inhibit;
    if (s_inhibit && s_sleeping) {
        exitSleep();
    }
    if (s_inhibit && s_display_standby) {
        exitDisplayStandby();
    }
    if (s_inhibit) {
        resetIdleState();
    }
}

bool isInhibited()
{
    return s_inhibit;
}

bool isSleeping()
{
    return s_sleeping || s_display_standby;
}

void markActivity()
{
    if (s_sleeping) {
        return;
    }

    if (s_display_standby) {
        exitDisplayStandby();
    }

    s_last_activity_ms = GetHAL().millis();
    s_sleep_orientation_count = 0;
    s_wake_orientation_count = 0;
    s_motion_wake_candidate = false;
    s_motion_wake_candidate_ms = 0;
}

void wake()
{
    if (s_display_standby) {
        exitDisplayStandby();
        markActivity();
        return;
    }

    exitSleep();
}

void requestNetworkResume()
{
    mclog::tagInfo(TAG, "network resume requested");

    if (s_display_standby) {
        exitDisplayStandby();
        markActivity();
    }

    common::wifi::setRecoveryPaused(false);

    const esp_err_t start_err = esp_wifi_start();
    if (start_err != ESP_OK &&
        start_err != ESP_ERR_WIFI_NOT_INIT &&
        start_err != ESP_ERR_INVALID_STATE) {
        mclog::tagWarn(TAG, "esp_wifi_start failed: {}", esp_err_to_name(start_err));
    }

    common::mqtt::setRecoveryPaused(false);
}

}  // namespace sleep_manager
