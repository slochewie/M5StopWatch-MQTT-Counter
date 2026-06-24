/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_setup.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <counter_service.h>
#include <location_presets.h>
#include <hal/utils/configure_ap/configure_ap.h>
#include <apps/common/network/wifi_service.h>
#include <apps/common/network/mqtt_service.h>
#include <apps/common/sleep_manager/sleep_manager.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_wifi.h>
#include <string>

using namespace mooncake;
using namespace view;
using namespace setup_workers;

namespace {
bool s_setup_appliance_mode = true;

bool s_setup_portal_active = false;

void resumeWifiRecoveryIfAwake()
{
    if (sleep_manager::isSleeping()) {
        mclog::tagInfo("Settings", "Wi-Fi recovery resume skipped; SleepManager is sleeping");
        return;
    }

    common::wifi::setRecoveryPaused(false);
    common::mqtt::setRecoveryPaused(false);
}

void forceWifiStaOnlyAfterPortal()
{
    const esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) {
        mclog::tagInfo("Settings", "Wi-Fi mode restored to STA-only after configure portal");
    } else {
        mclog::tagWarn("Settings", "Failed to restore Wi-Fi STA-only mode: {}", static_cast<int>(err));
    }
}




class APModeWorker : public WorkerBase {
public:
    APModeWorker()
    {
        rebuildMenuSections();
        _menu_page = std::make_unique<view::SelectMenuPage>(_menu_sections);
    }

    ~APModeWorker() override
    {
        _menu_sections.clear();
        _menu_page.reset();
    }

    void update() override
    {
        if (_start_requested) {
            _start_requested = false;
            startPortal();
        }

        if (_menu_page) {
            _menu_page->update();
        }
    }

private:
    std::vector<view::SelectMenuPage::MenuSection> _menu_sections;
    std::unique_ptr<view::SelectMenuPage> _menu_page;
    bool _start_requested = false;

    void rebuildMenuSections()
    {
        _menu_sections = {
            {
                "AP Mode",
                {
                    {s_setup_portal_active ? "Portal Running" : "Start Portal",
                     [this]() {
                         _start_requested = true;
                     }},
                },
            },
        };
    }

    void startPortal()
    {
        if (s_setup_portal_active) {
            return;
        }

        s_setup_portal_active = true;
        configure_ap::setRunning(true);
        common::mqtt::setRecoveryPaused(true);
        common::wifi::setRecoveryPaused(true);

        BaseType_t created = xTaskCreatePinnedToCore(
            APModeWorker::portalTask,
            "settings_portal",
            8192,
            nullptr,
            4,
            nullptr,
            0);

        if (created != pdPASS) {
            s_setup_portal_active = false;
            configure_ap::setRunning(false);
            resumeWifiRecoveryIfAwake();
        }

        rebuildMenuSections();
        _menu_page = std::make_unique<view::SelectMenuPage>(_menu_sections);
    }

    static void portalTask(void* arg)
    {
        (void)arg;

        configure_ap::run([](std::string_view msg) {
            std::string copy(msg);
            mclog::tagInfo("Settings", "Configure portal: {}", copy);
        });

        s_setup_portal_active = false;
        configure_ap::setRunning(false);
        forceWifiStaOnlyAfterPortal();
        resumeWifiRecoveryIfAwake();

        vTaskDelete(nullptr);
    }
};

class StartupAppWorker : public WorkerBase {
public:
    StartupAppWorker()
    {
        rebuildMenuSections();
        _menu_page = std::make_unique<view::SelectMenuPage>(_menu_sections);
    }

    ~StartupAppWorker() override
    {
        _menu_sections.clear();
        _menu_page.reset();
    }

    void update() override
    {
        if (_menu_page) {
            _menu_page->update();
        }
    }

private:
    std::vector<view::SelectMenuPage::MenuSection> _menu_sections;
    std::unique_ptr<view::SelectMenuPage> _menu_page;

    void rebuildMenuSections()
    {
        const auto startup_app = counter_service::getStartupApp();

        _menu_sections = {
            {
                "Startup App",
                {
                    {fmt::format("{} Counter", startup_app == counter_service::StartupApp::Counter ? LV_SYMBOL_OK : "  "),
                     [this]() {
                         counter_service::setStartupApp(counter_service::StartupApp::Counter, true);
                         _is_done = true;
                     }},
                    {fmt::format("{} Launcher", startup_app == counter_service::StartupApp::Launcher ? LV_SYMBOL_OK : "  "),
                     [this]() {
                         counter_service::setStartupApp(counter_service::StartupApp::Launcher, true);
                         _is_done = true;
                     }},
                },
            },
        };
    }
};

class LocationWorker : public WorkerBase {
public:
    explicit LocationWorker(bool& need_warm_reset)
        : _need_warm_reset(need_warm_reset)
    {
        rebuildMenuSections();
        _menu_page = std::make_unique<view::SelectMenuPage>(_menu_sections);
    }

    ~LocationWorker() override
    {
        _menu_sections.clear();
        _menu_page.reset();
    }

    void update() override
    {
        if (_menu_page) {
            _menu_page->update();
        }
    }

private:
    bool& _need_warm_reset;
    std::vector<view::SelectMenuPage::MenuSection> _menu_sections;
    std::unique_ptr<view::SelectMenuPage> _menu_page;

    void rebuildMenuSections()
    {
        const int selected_location = location_presets::selectedIndex();

        view::SelectMenuPage::MenuSection location_section = {
            "Location",
            {},
        };

        const size_t preset_count = location_presets::count();
        for (size_t i = 0; i < preset_count; ++i) {
            const auto* preset = location_presets::at(i);
            if (preset == nullptr || preset->name == nullptr || preset->name[0] == '\0') {
                continue;
            }

            const int preset_index = static_cast<int>(i);
            location_section.items.push_back(
                {
                    fmt::format("{} {}", selected_location == preset_index ? LV_SYMBOL_OK : "  ", preset->name),
                    [this, preset_index]() {
                        if (location_presets::apply(static_cast<size_t>(preset_index))) {
                            _need_warm_reset = true;
                            _is_done = true;
                        }
                    },
                });
        }

        if (location_section.items.empty()) {
            location_section.items.push_back(
                {
                    "No presets generated",
                    [this]() {
                        _is_done = true;
                    },
                });
        }

        _menu_sections = {
            location_section,
        };
    }
};


class WifiWorker : public WorkerBase {
public:
    explicit WifiWorker(bool& need_warm_reset)
        : _need_warm_reset(need_warm_reset)
    {
        rebuildMenuSections();
        _menu_page = std::make_unique<view::SelectMenuPage>(_menu_sections);
    }

    ~WifiWorker() override
    {
        _menu_sections.clear();
        _menu_page.reset();
    }

    void update() override
    {
        if (_menu_page) {
            _menu_page->update();
        }
    }

private:
    std::vector<view::SelectMenuPage::MenuSection> _menu_sections;
    std::unique_ptr<view::SelectMenuPage> _menu_page;
    bool& _need_warm_reset;

    void addChannelItem(std::vector<view::SelectMenuPage::MenuItem>& items, uint8_t channel, const char* label)
    {
        const uint8_t selected_channel = counter_service::wifiChannel();
        items.push_back(
            {
                fmt::format("{} {}", selected_channel == channel ? LV_SYMBOL_OK : "  ", label),
                [this, channel]() {
                    counter_service::setWifiChannel(channel, true);
                    _need_warm_reset = true;
                    _is_done = true;
                },
            });
    }

    void rebuildMenuSections()
    {
        const bool wifi_enabled = counter_service::isWifiEnabled();
        std::vector<view::SelectMenuPage::MenuItem> items = {
            {fmt::format("{} On", wifi_enabled ? LV_SYMBOL_OK : "  "),
             [this]() {
                 counter_service::setWifiEnabled(true, true);
                 _is_done = true;
             }},
            {fmt::format("{} Off", !wifi_enabled ? LV_SYMBOL_OK : "  "),
             [this]() {
                 counter_service::setWifiEnabled(false, true);
                 _is_done = true;
             }},
        };

        addChannelItem(items, 0, "Channel: Auto");
        addChannelItem(items, 1, "Channel: 1");
        addChannelItem(items, 6, "Channel: 6");
        addChannelItem(items, 11, "Channel: 11");

        _menu_sections = {
            {
                "WiFi",
                items,
            },
        };
    }
};

class MqttWorker : public WorkerBase {
public:
    MqttWorker()
    {
        rebuildMenuSections();
        _menu_page = std::make_unique<view::SelectMenuPage>(_menu_sections);
    }

    ~MqttWorker() override
    {
        _menu_sections.clear();
        _menu_page.reset();
    }

    void update() override
    {
        if (_menu_page) {
            _menu_page->update();
        }
    }

private:
    std::vector<view::SelectMenuPage::MenuSection> _menu_sections;
    std::unique_ptr<view::SelectMenuPage> _menu_page;

    void rebuildMenuSections()
    {
        const bool mqtt_enabled = counter_service::isMqttEnabled();

        _menu_sections = {
            {
                "MQTT",
                {
                    {fmt::format("{} On", mqtt_enabled ? LV_SYMBOL_OK : "  "),
                     [this]() {
                         counter_service::setMqttEnabled(true, true);
                         _is_done = true;
                     }},
                    {fmt::format("{} Off", !mqtt_enabled ? LV_SYMBOL_OK : "  "),
                     [this]() {
                         counter_service::setMqttEnabled(false, true);
                         _is_done = true;
                     }},
                },
            },
        };
    }
};

class ApplianceModeWorker : public WorkerBase {
public:
    ApplianceModeWorker(bool& setup_appliance_mode, bool& need_warm_reset)
        : _setup_appliance_mode(setup_appliance_mode), _need_warm_reset(need_warm_reset)
    {
        rebuildMenuSections();
        _menu_page = std::make_unique<view::SelectMenuPage>(_menu_sections);
    }

    ~ApplianceModeWorker() override
    {
        _menu_sections.clear();
        _menu_page.reset();
    }

    void update() override
    {
        if (_menu_page) {
            _menu_page->update();
        }
    }

private:
    bool& _setup_appliance_mode;
    bool& _need_warm_reset;
    std::vector<view::SelectMenuPage::MenuSection> _menu_sections;
    std::unique_ptr<view::SelectMenuPage> _menu_page;

    void rebuildMenuSections()
    {
        _menu_sections = {
            {
                "Appliance Mode",
                {
                    {fmt::format("{} On", _setup_appliance_mode ? LV_SYMBOL_OK : "  "),
                     [this]() {
                         _setup_appliance_mode = true;
                         GetHAL().setCounterApplianceMode(true, true);
                         _need_warm_reset = true;
                         _is_done = true;
                     }},
                    {fmt::format("{} Off", !_setup_appliance_mode ? LV_SYMBOL_OK : "  "),
                     [this]() {
                         _setup_appliance_mode = false;
                         GetHAL().setCounterApplianceMode(false, true);
                         _need_warm_reset = true;
                         _is_done = true;
                     }},
                },
            },
        };
    }
};

}  // namespace

AppSetup::AppSetup()
{
    setAppInfo().name = "Settings";
    setAppInfo().icon = (void*)&icon_setup;
}

void AppSetup::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
    // open();
}

void AppSetup::rebuildMenuSections()
{

    _menu_sections = {
        {
            "Device",
            {
                {"Brightness",
                 [&]() {
                     _destroy_menu = true;
                     _worker       = std::make_unique<BrightnessWorker>();
                 }},
                {"Volume",
                 [&]() {
                     _destroy_menu = true;
                     _worker       = std::make_unique<VolumeWorker>();
                 }},
                {"Button",
                 [&]() {
                     _destroy_menu = true;
                     _worker       = std::make_unique<ButtonWorker>();
                 }},
                {fmt::format("WiFi: {}", counter_service::isWifiEnabled() ? "On" : "Off"),
                 [&]() {
                     _destroy_menu = true;
                     _worker       = std::make_unique<WifiWorker>(_need_warm_reset);
                 }},
                {fmt::format("MQTT: {}", counter_service::isMqttEnabled() ? "On" : "Off"),
                 [&]() {
                     _destroy_menu = true;
                     _worker       = std::make_unique<MqttWorker>();
                 }},
                {fmt::format("Location: {}", location_presets::selectedName()),
                 [&]() {
                     _destroy_menu = true;
                     _worker       = std::make_unique<LocationWorker>(_need_warm_reset);
                 }},
            },
        },
        {
            "AP Mode",
            {
                {s_setup_portal_active ? "AP Mode: Running" : "AP Mode: Off",
                 [&]() {
                     _destroy_menu = true;
                     _worker       = std::make_unique<APModeWorker>();
                 }},
            },
        },
        {
            "Appliance Mode",
            {
                {fmt::format("Appliance Mode: {}", s_setup_appliance_mode ? "On" : "Off"),
                 [&]() {
                     _destroy_menu = true;
                     _worker       = std::make_unique<ApplianceModeWorker>(s_setup_appliance_mode, _need_warm_reset);
                 }},
            },
        },
        {
            "Startup App",
            {
                {fmt::format("Startup App: {}", counter_service::startupAppName()),
                 [&]() {
                     _destroy_menu = true;
                     _worker       = std::make_unique<StartupAppWorker>();
                 }},
            },
        },
        {
            "Time & Date",
            {
                {"Set Time",
                 [&]() {
                     _destroy_menu = true;
                     _worker       = std::make_unique<SetTimeWorker>();
                 }},
                {"Set Date",
                 [&]() {
                     _destroy_menu = true;
                     _worker       = std::make_unique<SetDateWorker>();
                 }},
            },
        },
        {
            "Firmware",
            {
                {fmt::format("Version: {}", common::FirmwareVersion),
                 [&]() {
                     _magic_count++;
                     if (_magic_count >= 10) {
                         _magic_count  = 0;
                         _destroy_menu = true;
                         _worker       = std::make_unique<AboutWorker>();
                     }
                 }},
            },
        },
    };
}

void AppSetup::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _key_manager = std::make_unique<input::KeyManager>();

    // Reset state
    _destroy_menu    = false;
    _rebuild_menu    = false;
    _need_warm_reset = false;
    _magic_count     = 0;

    s_setup_appliance_mode = GetHAL().isCounterApplianceMode(true);

    rebuildMenuSections();

    LvglLockGuard lock;

    _menu_page = std::make_unique<view::SelectMenuPage>(_menu_sections);
}

void AppSetup::onRunning()
{
    if (_key_manager && _key_manager->update() == input::KeyEvent::GoHome) {
        close();
        return;
    }

    LvglLockGuard lock;

    if (_menu_page) {
        _menu_page->update();
    }

    if (_rebuild_menu) {
        rebuildMenuSections();
        _menu_page = std::make_unique<view::SelectMenuPage>(_menu_sections);
        _rebuild_menu = false;
    }

    if (_destroy_menu) {
        _menu_page.reset();
        _destroy_menu = false;
    }

    if (_worker) {
        _worker->update();
        if (_worker->isDone()) {
            _worker.reset();
            rebuildMenuSections();
            _menu_page = std::make_unique<view::SelectMenuPage>(_menu_sections);
        }
    }
}

void AppSetup::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    _key_manager.reset();

    LvglLockGuard lock;

    _menu_sections.clear();
    _menu_page.reset();
    _worker.reset();

    if (_need_warm_reset) {
        GetHAL().delay(250);
        GetHAL().reboot();
    }
}
