#include "app_setup.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <counter_service.h>
#include <location_presets.h>

using namespace mooncake;
using namespace view;
using namespace setup_workers;

namespace {
bool s_setup_appliance_mode = true;
}  // namespace

AppSetup::AppSetup()
{
    setAppInfo().name = "Settings";
    setAppInfo().icon = (void*)&icon_setup;
}

void AppSetup::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppSetup::rebuildMenuSections()
{
    const auto startup_app = counter_service::getStartupApp();
    const int selected_location = location_presets::selectedIndex();

    _menu_sections = {
        {
            "Device",
            {
                {"Brightness", [&]() { _destroy_menu = true; _worker = std::make_unique<BrightnessWorker>(); }},
                {"Volume", [&]() { _destroy_menu = true; _worker = std::make_unique<VolumeWorker>(); }},
                {"Button", [&]() { _destroy_menu = true; _worker = std::make_unique<ButtonWorker>(); }},
                {fmt::format("WiFi: {}", counter_service::isWifiEnabled() ? "On" : "Off"), [&]() { counter_service::setWifiEnabled(!counter_service::isWifiEnabled(), true); _rebuild_menu = true; }},
                {fmt::format("MQTT: {}", counter_service::isMqttEnabled() ? "On" : "Off"), [&]() { counter_service::setMqttEnabled(!counter_service::isMqttEnabled(), true); _rebuild_menu = true; }},
            },
        },
        {
            "Location",
            {
                {fmt::format("{} Home", selected_location == 0 ? LV_SYMBOL_OK : "  "), [&]() { if (location_presets::apply(0)) { _need_warm_reset = true; } _rebuild_menu = true; }},
                {fmt::format("{} McCarthys", selected_location == 1 ? LV_SYMBOL_OK : "  "), [&]() { if (location_presets::apply(1)) { _need_warm_reset = true; } _rebuild_menu = true; }},
                {fmt::format("{} Library", selected_location == 2 ? LV_SYMBOL_OK : "  "), [&]() { if (location_presets::apply(2)) { _need_warm_reset = true; } _rebuild_menu = true; }},
                {fmt::format("{} Frog Peach", selected_location == 3 ? LV_SYMBOL_OK : "  "), [&]() { if (location_presets::apply(3)) { _need_warm_reset = true; } _rebuild_menu = true; }},
                {fmt::format("{} Bulls", selected_location == 4 ? LV_SYMBOL_OK : "  "), [&]() { if (location_presets::apply(4)) { _need_warm_reset = true; } _rebuild_menu = true; }},
            },
        },
        {
            "Appliance Mode",
            {
                {fmt::format("{} On", s_setup_appliance_mode ? LV_SYMBOL_OK : "  "), [&]() { s_setup_appliance_mode = true; GetHAL().setCounterApplianceMode(true, true); _need_warm_reset = true; _rebuild_menu = true; }},
                {fmt::format("{} Off", !s_setup_appliance_mode ? LV_SYMBOL_OK : "  "), [&]() { s_setup_appliance_mode = false; GetHAL().setCounterApplianceMode(false, true); _need_warm_reset = true; _rebuild_menu = true; }},
            },
        },
        {
            "Startup App",
            {
                {fmt::format("{} Counter", startup_app == counter_service::StartupApp::Counter ? LV_SYMBOL_OK : "  "), [&]() { counter_service::setStartupApp(counter_service::StartupApp::Counter, true); _rebuild_menu = true; }},
                {fmt::format("{} Launcher", startup_app == counter_service::StartupApp::Launcher ? LV_SYMBOL_OK : "  "), [&]() { counter_service::setStartupApp(counter_service::StartupApp::Launcher, true); _rebuild_menu = true; }},
            },
        },
        {
            "Time & Date",
            {
                {"Set Time", [&]() { _destroy_menu = true; _worker = std::make_unique<SetTimeWorker>(); }},
                {"Set Date", [&]() { _destroy_menu = true; _worker = std::make_unique<SetDateWorker>(); }},
            },
        },
        {
            "Firmware",
            {
                {fmt::format("Version: {}", common::FirmwareVersion), [&]() { _magic_count++; if (_magic_count >= 10) { _magic_count = 0; _destroy_menu = true; _worker = std::make_unique<AboutWorker>(); } }},
            },
        },
    };
}

void AppSetup::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");
    _key_manager = std::make_unique<input::KeyManager>();
    _destroy_menu = false;
    _rebuild_menu = false;
    _need_warm_reset = false;
    _magic_count = 0;
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
    if (_menu_page) { _menu_page->update(); }
    if (_rebuild_menu) { rebuildMenuSections(); _menu_page = std::make_unique<view::SelectMenuPage>(_menu_sections); _rebuild_menu = false; }
    if (_destroy_menu) { _menu_page.reset(); _destroy_menu = false; }
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
