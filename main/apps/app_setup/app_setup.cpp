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

struct WakeTimeoutOption {
    uint32_t ms;
    const char* label;
};

static constexpr WakeTimeoutOption kSoftSleepOptions[] = {
    {0, "Never"},
    {15000, "15 seconds"},
    {30000, "30 seconds"},
    {45000, "45 seconds"},
    {60000, "1 minute"},
    {120000, "2 minutes"},
};

static constexpr WakeTimeoutOption kDeepSleepOptions[] = {
    {0, "Never"},
    {30000, "30 seconds"},
    {45000, "45 seconds"},
    {60000, "1 minute"},
    {120000, "2 minutes"},
    {300000, "5 minutes"},
    {600000, "10 minutes"},
    {1800000, "30 minutes"},
};

size_t wakeOptionIndexForMs(const WakeTimeoutOption* options, size_t count, uint32_t ms, size_t fallbackIndex)
{
    for (size_t i = 0; i < count; ++i) {
        if (options[i].ms == ms) {
            return i;
        }
    }
    return fallbackIndex;
}
class WakeSettingsWorker : public WorkerBase {
public:
    WakeSettingsWorker()
        : _selected_soft_sleep_ms(sleep_manager::softSleepTimeoutMs()),
          _selected_deep_sleep_ms(sleep_manager::deepSleepTimeoutMs())
    {
        _view = std::make_unique<WakeSettingsView>(_selected_soft_sleep_ms, _selected_deep_sleep_ms);
    }

    ~WakeSettingsWorker() override
    {
        _view.reset();
    }

    void update() override
    {
        if (!_view) {
            return;
        }

        _selected_soft_sleep_ms = _view->currentSoftSleepMs();
        _selected_deep_sleep_ms = _view->currentDeepSleepMs();

        if (_view->consumeSaveRequested()) {
            sleep_manager::setSoftSleepTimeoutMs(_selected_soft_sleep_ms, true);
            sleep_manager::setDeepSleepTimeoutMs(_selected_deep_sleep_ms, true);
            _is_done = true;
        }
    }

private:
    class WakeSettingsView {
    public:
        WakeSettingsView(uint32_t initialSoftSleepMs, uint32_t initialDeepSleepMs)
            : _soft_sleep_index(wakeOptionIndexForMs(kSoftSleepOptions,
                                                     sizeof(kSoftSleepOptions) / sizeof(kSoftSleepOptions[0]),
                                                     initialSoftSleepMs,
                                                     1)),
              _deep_sleep_index(wakeOptionIndexForMs(kDeepSleepOptions,
                                                     sizeof(kDeepSleepOptions) / sizeof(kDeepSleepOptions[0]),
                                                     initialDeepSleepMs,
                                                     2))
        {
            _panel = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(lv_screen_active());
            _panel->align(LV_ALIGN_CENTER, 0, 0);
            _panel->setSize(466, 466);
            _panel->setRadius(0);
            _panel->setBorderWidth(0);
            _panel->setPaddingAll(0);
            _panel->setBgColor(lv_color_hex(0x000000));
            _panel->setBgOpa(LV_OPA_COVER);
            _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

            _title_label = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_panel->get());
            _title_label->setText("Wake Settings");
            _title_label->setTextFont(&lv_font_montserrat_28);
            _title_label->setTextColor(lv_color_hex(0xFFFFFF));
            _title_label->align(LV_ALIGN_TOP_MID, 0, 34);

            createSoftSleepButton(105);
            createDeepSleepButton(225);

            _ok_button = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Button>(_panel->get());
            _ok_button->align(LV_ALIGN_CENTER, 0, 175);
            _ok_button->setSize(374, 100);
            _ok_button->setRadius(50);
            _ok_button->setBorderWidth(0);
            _ok_button->setShadowWidth(0);
            _ok_button->setBgColor(lv_color_hex(0x4AD78C));
            _ok_button->label().setText("OK");
            _ok_button->label().setTextFont(&lv_font_montserrat_28);
            _ok_button->label().setTextColor(lv_color_hex(0x0F5831));
            _ok_button->label().align(LV_ALIGN_CENTER, 0, 0);
            _ok_button->onClick().connect([this]() { _save_requested = true; });

            updateLabels();
        }

        uint32_t currentSoftSleepMs() const
        {
            return kSoftSleepOptions[_soft_sleep_index].ms;
        }

        uint32_t currentDeepSleepMs() const
        {
            return kDeepSleepOptions[_deep_sleep_index].ms;
        }

        bool consumeSaveRequested()
        {
            bool requested  = _save_requested;
            _save_requested = false;
            return requested;
        }

    private:
        void createSoftSleepButton(int y)
        {
            _soft_sleep_button = createOptionButton(y, [this]() {
                _soft_sleep_index = (_soft_sleep_index + 1) % (sizeof(kSoftSleepOptions) / sizeof(kSoftSleepOptions[0]));
                updateLabels();
            });
        }

        void createDeepSleepButton(int y)
        {
            _deep_sleep_button = createOptionButton(y, [this]() {
                _deep_sleep_index = (_deep_sleep_index + 1) % (sizeof(kDeepSleepOptions) / sizeof(kDeepSleepOptions[0]));
                updateLabels();
            });
        }

        std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> createOptionButton(int y, std::function<void()> onClick)
        {
            auto button = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Button>(_panel->get());
            button->align(LV_ALIGN_TOP_MID, 0, y);
            button->setSize(374, 104);
            button->setRadius(52);
            button->setBorderWidth(0);
            button->setShadowWidth(0);
            button->setBgColor(lv_color_hex(0x4C4C4C));
            button->label().setTextFont(&lv_font_montserrat_22);
            button->label().setTextColor(lv_color_hex(0xFFFFFF));
            button->label().align(LV_ALIGN_CENTER, 0, 0);
            button->onClick().connect(std::move(onClick));
            return button;
        }

        void updateLabels()
        {
            _soft_sleep_button->label().setText(fmt::format("Soft Sleep\n> {}", kSoftSleepOptions[_soft_sleep_index].label).c_str());
            _deep_sleep_button->label().setText(fmt::format("Deep Sleep\n> {}", kDeepSleepOptions[_deep_sleep_index].label).c_str());
        }

        std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _panel;
        std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _title_label;
        std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _soft_sleep_button;
        std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _deep_sleep_button;
        std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _ok_button;
        size_t _soft_sleep_index;
        size_t _deep_sleep_index;
        bool _save_requested = false;
    };

    uint32_t _selected_soft_sleep_ms;
    uint32_t _selected_deep_sleep_ms;
    std::unique_ptr<WakeSettingsView> _view;
};

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
        : _selected_startup_app(counter_service::getStartupApp())
    {
        _view = std::make_unique<StartupAppView>(_selected_startup_app);
    }

    ~StartupAppWorker() override
    {
        _view.reset();
    }

    void update() override
    {
        if (!_view) {
            return;
        }

        _selected_startup_app = _view->currentStartupApp();

        if (_view->consumeSaveRequested()) {
            counter_service::setStartupApp(_selected_startup_app, true);
            _is_done = true;
        }
    }

private:
    class StartupAppView {
    public:
        explicit StartupAppView(counter_service::StartupApp initialStartupApp)
            : _current_startup_app(initialStartupApp)
        {
            _panel = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(lv_screen_active());
            _panel->align(LV_ALIGN_CENTER, 0, 0);
            _panel->setSize(466, 466);
            _panel->setRadius(0);
            _panel->setBorderWidth(0);
            _panel->setPaddingAll(0);
            _panel->setBgColor(lv_color_hex(0x000000));
            _panel->setBgOpa(LV_OPA_COVER);
            _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

            createOptionButton(70, counter_service::StartupApp::Counter, "Counter");
            createOptionButton(205, counter_service::StartupApp::Launcher, "Launcher");

            _ok_button = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Button>(_panel->get());
            _ok_button->align(LV_ALIGN_CENTER, 0, 175);
            _ok_button->setSize(374, 130);
            _ok_button->setRadius(77);
            _ok_button->setBorderWidth(0);
            _ok_button->setShadowWidth(0);
            _ok_button->setBgColor(lv_color_hex(0x4AD78C));
            _ok_button->label().setText("OK");
            _ok_button->label().setTextFont(&lv_font_montserrat_28);
            _ok_button->label().setTextColor(lv_color_hex(0x0F5831));
            _ok_button->label().align(LV_ALIGN_CENTER, 0, 0);
            _ok_button->onClick().connect([this]() { _save_requested = true; });

            updateOptionLabels();
        }

        counter_service::StartupApp currentStartupApp() const
        {
            return _current_startup_app;
        }

        bool consumeSaveRequested()
        {
            bool requested  = _save_requested;
            _save_requested = false;
            return requested;
        }

    private:
        void createOptionButton(int y, counter_service::StartupApp startupApp, const char* label)
        {
            auto button = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Button>(_panel->get());
            button->align(LV_ALIGN_TOP_MID, 0, y);
            button->setSize(374, 119);
            button->setRadius(60);
            button->setBorderWidth(0);
            button->setShadowWidth(0);
            button->setBgColor(lv_color_hex(0x4C4C4C));
            button->label().setTextFont(&lv_font_montserrat_24);
            button->label().setTextColor(lv_color_hex(0xFFFFFF));
            button->label().align(LV_ALIGN_CENTER, 0, 0);
            button->onClick().connect([this, startupApp]() {
                _current_startup_app = startupApp;
                updateOptionLabels();
            });

            _option_labels.push_back(label);
            _option_values.push_back(startupApp);
            _option_buttons.push_back(std::move(button));
        }

        void updateOptionLabels()
        {
            for (size_t i = 0; i < _option_buttons.size(); ++i) {
                const bool selected = _option_values[i] == _current_startup_app;
                _option_buttons[i]->label().setText(fmt::format("{}{}", selected ? LV_SYMBOL_OK " " : "", _option_labels[i]).c_str());
            }
        }

        std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _panel;
        std::vector<std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button>> _option_buttons;
        std::vector<const char*> _option_labels;
        std::vector<counter_service::StartupApp> _option_values;
        std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _ok_button;
        counter_service::StartupApp _current_startup_app;
        bool _save_requested = false;
    };

    counter_service::StartupApp _selected_startup_app;
    std::unique_ptr<StartupAppView> _view;
};


class LocationWorker : public WorkerBase {
public:
    explicit LocationWorker(bool& need_warm_reset)
        : _need_warm_reset(need_warm_reset), _selected_location(location_presets::selectedIndex())
    {
        _view = std::make_unique<LocationView>(_selected_location);
    }

    ~LocationWorker() override
    {
        _view.reset();
    }

    void update() override
    {
        if (!_view) {
            return;
        }

        _selected_location = _view->currentLocationIndex();

        if (_view->consumeSaveRequested()) {
            if (_selected_location >= 0 && location_presets::apply(static_cast<size_t>(_selected_location))) {
                _need_warm_reset = true;
            }
            _is_done = true;
        }
    }

private:
    class LocationView {
    public:
        explicit LocationView(int initialLocation)
            : _current_location(initialLocation)
        {
            _panel = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(lv_screen_active());
            _panel->align(LV_ALIGN_CENTER, 0, 0);
            _panel->setSize(466, 466);
            _panel->setRadius(0);
            _panel->setBorderWidth(0);
            _panel->setPaddingAll(0);
            _panel->setBgColor(lv_color_hex(0x000000));
            _panel->setBgOpa(LV_OPA_COVER);
            _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

            _list_container = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(_panel->get());
            _list_container->align(LV_ALIGN_TOP_MID, 0, 0);
            _list_container->setSize(466, 326);
            _list_container->setRadius(0);
            _list_container->setBorderWidth(0);
            _list_container->setPaddingAll(0);
            _list_container->setBgColor(lv_color_hex(0x000000));
            _list_container->setBgOpa(LV_OPA_COVER);
            lv_obj_add_flag(_list_container->get(), LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_scroll_dir(_list_container->get(), LV_DIR_VER);
            lv_obj_set_scrollbar_mode(_list_container->get(), LV_SCROLLBAR_MODE_AUTO);

            const size_t preset_count = location_presets::count();
            int button_y = 10;
            for (size_t i = 0; i < preset_count; ++i) {
                const auto* preset = location_presets::at(i);
                if (preset == nullptr || preset->name == nullptr || preset->name[0] == '\0') {
                    continue;
                }

                createOptionButton(button_y, static_cast<int>(i), preset->name);
                button_y += 92;
            }

            if (_option_buttons.empty()) {
                createMessageLabel("No presets generated");
            }

            _ok_button = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Button>(_panel->get());
            _ok_button->align(LV_ALIGN_CENTER, 0, 175);
            _ok_button->setSize(374, 130);
            _ok_button->setRadius(77);
            _ok_button->setBorderWidth(0);
            _ok_button->setShadowWidth(0);
            _ok_button->setBgColor(lv_color_hex(0x4AD78C));
            _ok_button->label().setText("OK");
            _ok_button->label().setTextFont(&lv_font_montserrat_28);
            _ok_button->label().setTextColor(lv_color_hex(0x0F5831));
            _ok_button->label().align(LV_ALIGN_CENTER, 0, 0);
            _ok_button->onClick().connect([this]() { _save_requested = true; });

            updateOptionLabels();
        }

        int currentLocationIndex() const
        {
            return _current_location;
        }

        bool consumeSaveRequested()
        {
            bool requested  = _save_requested;
            _save_requested = false;
            return requested;
        }

    private:
        void createMessageLabel(const char* text)
        {
            _message_label = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_list_container->get());
            _message_label->setText(text);
            _message_label->setTextFont(&lv_font_montserrat_24);
            _message_label->setTextColor(lv_color_hex(0xFFFFFF));
            _message_label->align(LV_ALIGN_CENTER, 0, -60);
        }

        void createOptionButton(int y, int locationIndex, const char* label)
        {
            auto button = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Button>(_list_container->get());
            button->align(LV_ALIGN_TOP_MID, 0, y);
            button->setSize(374, 82);
            button->setRadius(41);
            button->setBorderWidth(0);
            button->setShadowWidth(0);
            button->setBgColor(lv_color_hex(0x4C4C4C));
            button->label().setTextFont(&lv_font_montserrat_20);
            button->label().setTextColor(lv_color_hex(0xFFFFFF));
            button->label().align(LV_ALIGN_CENTER, 0, 0);
            button->onClick().connect([this, locationIndex]() {
                _current_location = locationIndex;
                updateOptionLabels();
            });

            _option_labels.push_back(label);
            _option_values.push_back(locationIndex);
            _option_buttons.push_back(std::move(button));
        }

        void updateOptionLabels()
        {
            for (size_t i = 0; i < _option_buttons.size(); ++i) {
                const bool selected = _option_values[i] == _current_location;
                _option_buttons[i]->label().setText(fmt::format("{}{}", selected ? LV_SYMBOL_OK " " : "", _option_labels[i]).c_str());
            }
        }

        std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _panel;
        std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _list_container;
        std::vector<std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button>> _option_buttons;
        std::vector<const char*> _option_labels;
        std::vector<int> _option_values;
        std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _ok_button;
        std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _message_label;
        int _current_location;
        bool _save_requested = false;
    };

    bool& _need_warm_reset;
    int _selected_location;
    std::unique_ptr<LocationView> _view;
};


class WifiWorker : public WorkerBase {
public:
    explicit WifiWorker(bool& need_warm_reset)
        : _need_warm_reset(need_warm_reset),
          _selected_wifi_enabled(counter_service::isWifiEnabled()),
          _selected_wifi_channel(counter_service::wifiChannel())
    {
        _view = std::make_unique<WifiView>(_selected_wifi_enabled, _selected_wifi_channel);
    }

    ~WifiWorker() override
    {
        _view.reset();
    }

    void update() override
    {
        if (!_view) {
            return;
        }

        _selected_wifi_enabled = _view->currentWifiEnabled();
        _selected_wifi_channel = _view->currentWifiChannel();

        if (_view->consumeSaveRequested()) {
            counter_service::setWifiEnabled(_selected_wifi_enabled, true);
            counter_service::setWifiChannel(_selected_wifi_channel, true);
            _need_warm_reset = true;
            _is_done = true;
        }
    }

private:
    class WifiView {
    public:
        WifiView(bool initialWifiEnabled, uint8_t initialWifiChannel)
            : _current_wifi_enabled(initialWifiEnabled), _current_wifi_channel(initialWifiChannel)
        {
            _panel = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(lv_screen_active());
            _panel->align(LV_ALIGN_CENTER, 0, 0);
            _panel->setSize(466, 466);
            _panel->setRadius(0);
            _panel->setBorderWidth(0);
            _panel->setPaddingAll(0);
            _panel->setBgColor(lv_color_hex(0x000000));
            _panel->setBgOpa(LV_OPA_COVER);
            _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

          createWifiSwitchRow(42, "WiFi", _current_wifi_enabled);

          createChannelButton(-98, 170, 0, "Auto");
          createChannelButton(98, 170, 1, "Ch 1");
          createChannelButton(-98, 258, 6, "Ch 6");
          createChannelButton(98, 258, 11, "Ch 11");

            _ok_button = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Button>(_panel->get());
            _ok_button->align(LV_ALIGN_CENTER, 0, 175);
            _ok_button->setSize(374, 130);
            _ok_button->setRadius(77);
            _ok_button->setBorderWidth(0);
            _ok_button->setShadowWidth(0);
            _ok_button->setBgColor(lv_color_hex(0x4AD78C));
            _ok_button->label().setText("OK");
            _ok_button->label().setTextFont(&lv_font_montserrat_28);
            _ok_button->label().setTextColor(lv_color_hex(0x0F5831));
            _ok_button->label().align(LV_ALIGN_CENTER, 0, 0);
            _ok_button->onClick().connect([this]() { _save_requested = true; });

            updateOptionLabels();
        }

        bool currentWifiEnabled() const
        {
            return _current_wifi_enabled;
        }

        uint8_t currentWifiChannel() const
        {
            return _current_wifi_channel;
        }

        bool consumeSaveRequested()
        {
            bool requested  = _save_requested;
            _save_requested = false;
            return requested;
        }

    private:
      void createWifiSwitchRow(int y, const char* title, bool initialValue)
      {
          auto row = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(_panel->get());
          row->setSize(374, 119);
          row->align(LV_ALIGN_TOP_MID, 0, y);
          row->setBgColor(lv_color_hex(0x4C4C4C));
          row->setBorderWidth(0);
          row->setShadowWidth(0);
          row->setRadius(60);
          row->setPaddingAll(0);
          row->setBgOpa(LV_OPA_COVER);

          auto label = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(row->get());
          label->setText(title);
          label->setTextFont(&lv_font_montserrat_24);
          label->setTextColor(lv_color_hex(0xFFFFFF));
          label->align(LV_ALIGN_LEFT_MID, 36, 0);

          auto switch_widget = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Switch>(row->get());
          switch_widget->setSize(80, 44);
          switch_widget->align(LV_ALIGN_RIGHT_MID, -36, 0);
          switch_widget->setValue(initialValue);
          switch_widget->setBgColor(lv_color_hex(0x3A3A3A), LV_PART_MAIN);
          switch_widget->setBgOpa(LV_OPA_COVER, LV_PART_MAIN);
          switch_widget->setBorderWidth(0, LV_PART_MAIN);
          switch_widget->setRadius(LV_RADIUS_CIRCLE, LV_PART_MAIN);
          lv_obj_set_style_bg_color(switch_widget->get(), lv_color_hex(0xFFFFFF), LV_PART_KNOB);
          lv_obj_set_style_bg_opa(switch_widget->get(), LV_OPA_COVER, LV_PART_KNOB);
          lv_obj_set_style_border_width(switch_widget->get(), 0, LV_PART_KNOB);
          lv_obj_set_style_radius(switch_widget->get(), LV_RADIUS_CIRCLE, LV_PART_KNOB);
          switch_widget->setBgColor(lv_color_hex(0x53BD65), static_cast<lv_style_selector_t>(LV_PART_INDICATOR) | LV_STATE_CHECKED);
          switch_widget->setBgOpa(LV_OPA_COVER, static_cast<lv_style_selector_t>(LV_PART_INDICATOR) | LV_STATE_CHECKED);
          switch_widget->setBorderWidth(0, static_cast<lv_style_selector_t>(LV_PART_INDICATOR) | LV_STATE_CHECKED);
          switch_widget->onValueChanged().connect([this](bool enabled) {
              _current_wifi_enabled = enabled;
          });

          _wifi_label = std::move(label);
          _wifi_switch = std::move(switch_widget);
          _wifi_row = std::move(row);
      }

        void createChannelButton(int x, int y, uint8_t channel, const char* label)
        {
            auto button = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Button>(_panel->get());
            button->align(LV_ALIGN_TOP_MID, x, y);
            button->setSize(178, 84);
            button->setRadius(42);
            button->setBorderWidth(0);
            button->setShadowWidth(0);
            button->setBgColor(lv_color_hex(0x4C4C4C));
            button->label().setTextFont(&lv_font_montserrat_20);
            button->label().setTextColor(lv_color_hex(0xFFFFFF));
            button->label().align(LV_ALIGN_CENTER, 0, 0);
            button->onClick().connect([this, channel]() {
                _current_wifi_channel = channel;
                updateOptionLabels();
            });

            _channel_labels.push_back(label);
            _channel_values.push_back(channel);
            _channel_buttons.push_back(std::move(button));
        }

        void updateOptionLabels()
        {

            for (size_t i = 0; i < _channel_buttons.size(); ++i) {
                const bool selected = _channel_values[i] == _current_wifi_channel;
                _channel_buttons[i]->label().setText(fmt::format("{}{}", selected ? LV_SYMBOL_OK " " : "", _channel_labels[i]).c_str());
            }
        }

        std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _panel;
        std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _wifi_row;
        std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _wifi_label;
        std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Switch> _wifi_switch;
        std::vector<std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button>> _channel_buttons;
        std::vector<const char*> _channel_labels;
        std::vector<uint8_t> _channel_values;
        std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _ok_button;
        bool _current_wifi_enabled;
        uint8_t _current_wifi_channel;
        bool _save_requested = false;
    };

    bool& _need_warm_reset;
    bool _selected_wifi_enabled;
    uint8_t _selected_wifi_channel;
    std::unique_ptr<WifiView> _view;
};

class MqttWorker : public WorkerBase {
public:
    MqttWorker()
        : _selected_mqtt_enabled(counter_service::isMqttEnabled())
    {
        _view = std::make_unique<MqttView>(_selected_mqtt_enabled);
    }

    ~MqttWorker() override
    {
        _view.reset();
    }

    void update() override
    {
        if (!_view) {
            return;
        }

        _selected_mqtt_enabled = _view->currentMqttEnabled();

        if (_view->consumeSaveRequested()) {
            counter_service::setMqttEnabled(_selected_mqtt_enabled, true);
            _is_done = true;
        }
    }

private:
    class MqttView {
    public:
        explicit MqttView(bool initialMqttEnabled)
            : _current_mqtt_enabled(initialMqttEnabled)
        {
            _panel = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(lv_screen_active());
            _panel->align(LV_ALIGN_CENTER, 0, 0);
            _panel->setSize(466, 466);
            _panel->setRadius(0);
            _panel->setBorderWidth(0);
            _panel->setPaddingAll(0);
            _panel->setBgColor(lv_color_hex(0x000000));
            _panel->setBgOpa(LV_OPA_COVER);
            _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

            createOptionButton(70, true, "On");
            createOptionButton(205, false, "Off");

            _ok_button = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Button>(_panel->get());
            _ok_button->align(LV_ALIGN_CENTER, 0, 175);
            _ok_button->setSize(374, 130);
            _ok_button->setRadius(77);
            _ok_button->setBorderWidth(0);
            _ok_button->setShadowWidth(0);
            _ok_button->setBgColor(lv_color_hex(0x4AD78C));
            _ok_button->label().setText("OK");
            _ok_button->label().setTextFont(&lv_font_montserrat_28);
            _ok_button->label().setTextColor(lv_color_hex(0x0F5831));
            _ok_button->label().align(LV_ALIGN_CENTER, 0, 0);
            _ok_button->onClick().connect([this]() { _save_requested = true; });

            updateOptionLabels();
        }

        bool currentMqttEnabled() const
        {
            return _current_mqtt_enabled;
        }

        bool consumeSaveRequested()
        {
            bool requested  = _save_requested;
            _save_requested = false;
            return requested;
        }

    private:
        void createOptionButton(int y, bool mqttEnabled, const char* label)
        {
            auto button = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Button>(_panel->get());
            button->align(LV_ALIGN_TOP_MID, 0, y);
            button->setSize(374, 119);
            button->setRadius(60);
            button->setBorderWidth(0);
            button->setShadowWidth(0);
            button->setBgColor(lv_color_hex(0x4C4C4C));
            button->label().setTextFont(&lv_font_montserrat_24);
            button->label().setTextColor(lv_color_hex(0xFFFFFF));
            button->label().align(LV_ALIGN_CENTER, 0, 0);
            button->onClick().connect([this, mqttEnabled]() {
                _current_mqtt_enabled = mqttEnabled;
                updateOptionLabels();
            });

            _option_labels.push_back(label);
            _option_values.push_back(mqttEnabled);
            _option_buttons.push_back(std::move(button));
        }

        void updateOptionLabels()
        {
            for (size_t i = 0; i < _option_buttons.size(); ++i) {
                const bool selected = _option_values[i] == _current_mqtt_enabled;
                _option_buttons[i]->label().setText(fmt::format("{}{}", selected ? LV_SYMBOL_OK " " : "", _option_labels[i]).c_str());
            }
        }

        std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _panel;
        std::vector<std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button>> _option_buttons;
        std::vector<const char*> _option_labels;
        std::vector<bool> _option_values;
        std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _ok_button;
        bool _current_mqtt_enabled;
        bool _save_requested = false;
    };

    bool _selected_mqtt_enabled;
    std::unique_ptr<MqttView> _view;
};

class ApplianceModeWorker : public WorkerBase {
public:
    ApplianceModeWorker(bool& setup_appliance_mode, bool& need_warm_reset)
        : _setup_appliance_mode(setup_appliance_mode), _need_warm_reset(need_warm_reset),
          _selected_appliance_mode(setup_appliance_mode)
    {
        _view = std::make_unique<ApplianceModeView>(_selected_appliance_mode);
    }

    ~ApplianceModeWorker() override
    {
        _view.reset();
    }

    void update() override
    {
        if (!_view) {
            return;
        }

        _selected_appliance_mode = _view->currentApplianceMode();

        if (_view->consumeSaveRequested()) {
            _setup_appliance_mode = _selected_appliance_mode;
            GetHAL().setCounterApplianceMode(_selected_appliance_mode, true);
            _need_warm_reset = true;
            _is_done = true;
        }
    }

private:
    class ApplianceModeView {
    public:
        explicit ApplianceModeView(bool initialApplianceMode)
            : _current_appliance_mode(initialApplianceMode)
        {
            _panel = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(lv_screen_active());
            _panel->align(LV_ALIGN_CENTER, 0, 0);
            _panel->setSize(466, 466);
            _panel->setRadius(0);
            _panel->setBorderWidth(0);
            _panel->setPaddingAll(0);
            _panel->setBgColor(lv_color_hex(0x000000));
            _panel->setBgOpa(LV_OPA_COVER);
            _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

            createOptionButton(70, true, "On");
            createOptionButton(205, false, "Off");

            _ok_button = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Button>(_panel->get());
            _ok_button->align(LV_ALIGN_CENTER, 0, 175);
            _ok_button->setSize(374, 130);
            _ok_button->setRadius(77);
            _ok_button->setBorderWidth(0);
            _ok_button->setShadowWidth(0);
            _ok_button->setBgColor(lv_color_hex(0x4AD78C));
            _ok_button->label().setText("OK");
            _ok_button->label().setTextFont(&lv_font_montserrat_28);
            _ok_button->label().setTextColor(lv_color_hex(0x0F5831));
            _ok_button->label().align(LV_ALIGN_CENTER, 0, 0);
            _ok_button->onClick().connect([this]() { _save_requested = true; });

            updateOptionLabels();
        }

        bool currentApplianceMode() const
        {
            return _current_appliance_mode;
        }

        bool consumeSaveRequested()
        {
            bool requested  = _save_requested;
            _save_requested = false;
            return requested;
        }

    private:
        void createOptionButton(int y, bool applianceMode, const char* label)
        {
            auto button = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Button>(_panel->get());
            button->align(LV_ALIGN_TOP_MID, 0, y);
            button->setSize(374, 119);
            button->setRadius(60);
            button->setBorderWidth(0);
            button->setShadowWidth(0);
            button->setBgColor(lv_color_hex(0x4C4C4C));
            button->label().setTextFont(&lv_font_montserrat_24);
            button->label().setTextColor(lv_color_hex(0xFFFFFF));
            button->label().align(LV_ALIGN_CENTER, 0, 0);
            button->onClick().connect([this, applianceMode]() {
                _current_appliance_mode = applianceMode;
                updateOptionLabels();
            });

            _option_labels.push_back(label);
            _option_values.push_back(applianceMode);
            _option_buttons.push_back(std::move(button));
        }

        void updateOptionLabels()
        {
            for (size_t i = 0; i < _option_buttons.size(); ++i) {
                const bool selected = _option_values[i] == _current_appliance_mode;
                _option_buttons[i]->label().setText(fmt::format("{}{}", selected ? LV_SYMBOL_OK " " : "", _option_labels[i]).c_str());
            }
        }

        std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _panel;
        std::vector<std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button>> _option_buttons;
        std::vector<const char*> _option_labels;
        std::vector<bool> _option_values;
        std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _ok_button;
        bool _current_appliance_mode;
        bool _save_requested = false;
    };

    bool& _setup_appliance_mode;
    bool& _need_warm_reset;
    bool _selected_appliance_mode;
    std::unique_ptr<ApplianceModeView> _view;
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
                {"Wake Settings",
                 [&]() {
                     _destroy_menu = true;
                     _worker       = std::make_unique<WakeSettingsWorker>();
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
