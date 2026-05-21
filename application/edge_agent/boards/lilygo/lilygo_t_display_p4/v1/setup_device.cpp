/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "esp_board_manager_includes.h"
#include "esp_check.h"
#include "esp_err.h"
#include "gen_board_device_custom.h"
#include "lilygo_device_driver_library.h"

namespace esp_claw_lilygo_t_display_p4 {
namespace {

using TDisplayP4Driver = lilygo_device_driver::TDisplayP4Driver;
using ScreenInfo = lilygo_device_driver::t_display_p4::device::ScreenInfo;
using ScreenType = lilygo_device_driver::t_display_p4::device::ScreenType;

constexpr char kTag[] = "esp_claw_lilygo_t_display_p4";
constexpr char kPowerCtrlDeviceName[] = "power_ctrl";
constexpr char kDisplayLcdDeviceName[] = "display_lcd";
constexpr char kLcdBrightnessDeviceName[] = "lcd_brightness";
constexpr uint8_t kDefaultBrightnessPercent = 100;

struct BrightnessHandle {
  bool (*set_percent)(uint8_t percent);
  uint8_t percent;
};

bool SetBrightnessPercent(uint8_t percent);

bool g_driver_objects_created = false;
bool g_power_initialized = false;
bool g_brightness_initialized = false;
bool g_xl9535_i2c_bound = false;
dev_display_lcd_handles_t g_screen_handles = {};
dev_display_lcd_config_t g_screen_config = {};
BrightnessHandle g_brightness_handle = {
    .set_percent = SetBrightnessPercent,
    .percent = kDefaultBrightnessPercent,
};

esp_err_t BindXl9535ToBoardI2c(const char* i2c_periph_name) {
  if (g_xl9535_i2c_bound) {
    return ESP_OK;
  }
  ESP_RETURN_ON_FALSE(i2c_periph_name != nullptr, ESP_ERR_INVALID_ARG, kTag,
      "I2C peripheral name is NULL");

  TDisplayP4Driver& driver = TDisplayP4Driver::GetInstance();
  if (!g_driver_objects_created) {
    driver.CreateDrivers();
    g_driver_objects_created = true;
  }

  void* i2c_bus_handle = nullptr;
  // Board peripherals are initialized before board devices; only borrow the
  // existing bus handle here, without changing the peripheral ref count.
  const esp_err_t ret =
      esp_board_periph_get_handle(i2c_periph_name, &i2c_bus_handle);
  ESP_RETURN_ON_ERROR(ret, kTag, "Failed to get I2C bus '%s'", i2c_periph_name);
  i2c_master_bus_handle_t i2c_bus =
      static_cast<i2c_master_bus_handle_t>(i2c_bus_handle);
  if (i2c_bus == nullptr) {
    ESP_RETURN_ON_FALSE(
        false, ESP_FAIL, kTag, "I2C bus '%s' handle is NULL", i2c_periph_name);
  }

  if (driver.bus().xl9535_i2c_bus == nullptr) {
    ESP_RETURN_ON_FALSE(false, ESP_FAIL, kTag, "XL9535 I2C bus driver is NULL");
  }
  if (!driver.bus().xl9535_i2c_bus->set_bus_handle(i2c_bus)) {
    ESP_RETURN_ON_FALSE(
        false, ESP_FAIL, kTag, "Set XL9535 I2C bus handle failed");
  }

  g_xl9535_i2c_bound = true;
  return ESP_OK;
}

esp_err_t InitPowerControlDevice(TDisplayP4Driver& driver) {
  ESP_RETURN_ON_FALSE(driver.InitXl9535(), ESP_FAIL, kTag, "InitXl9535 failed");
  ESP_RETURN_ON_FALSE(driver.InitPower(), ESP_FAIL, kTag, "InitPower failed");
  ESP_RETURN_ON_FALSE(
      driver.ConfigXl9535(), ESP_FAIL, kTag, "ConfigXl9535 failed");
  ESP_RETURN_ON_FALSE(
      driver.chip().xl9535 != nullptr, ESP_FAIL, kTag, "XL9535 driver is NULL");
  ESP_RETURN_ON_FALSE(driver.chip().xl9535->Deinit(false), ESP_FAIL, kTag,
      "XL9535 device deinit failed");
  return ESP_OK;
}

dev_display_lcd_config_t MakeDisplayConfig(const ScreenInfo& screen) {
  dev_display_lcd_config_t config = {};
  config.name = kDisplayLcdDeviceName;
  config.chip = screen.name;
  config.sub_type = ESP_BOARD_DEVICE_LCD_SUB_TYPE_DSI;
  config.lcd_width = static_cast<uint16_t>(screen.width);
  config.lcd_height = static_cast<uint16_t>(screen.height);
  config.swap_xy = 0;
  config.mirror_x = 0;
  config.mirror_y = 0;
  config.need_reset = 0;
  config.invert_color = 0;
  config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
  config.data_endian = LCD_RGB_DATA_ENDIAN_LITTLE;
  config.bits_per_pixel = screen.bits_per_pixel;
  return config;
}

bool UpdateDisplayHandles(TDisplayP4Driver& driver) {
  if (driver.bus().screen_mipi_bus == nullptr ||
      driver.bus().screen_mipi_bus->device_handle() == nullptr) {
    return false;
  }

  g_screen_handles.io_handle = nullptr;
  g_screen_handles.panel_handle = driver.bus().screen_mipi_bus->device_handle();
  return true;
}

bool DeinitBrightness(TDisplayP4Driver& driver) {
  bool result = true;

  if (g_brightness_initialized) {
    SetBrightnessPercent(0);
    result &= driver.DeinitScreenBacklight();
    g_brightness_initialized = false;
  }

  return result;
}

bool SetBrightnessPercent(uint8_t percent) {
  if (percent > 100) {
    percent = 100;
  }

  TDisplayP4Driver& driver = TDisplayP4Driver::GetInstance();
  switch (driver.screen_type()) {
    case ScreenType::kHi8561:
      if (!driver.status().hi8561_backlight.init_flag ||
          driver.chip().hi8561_backlight == nullptr ||
          !driver.chip().hi8561_backlight->SetDuty(percent)) {
        return false;
      }
      break;
    case ScreenType::kRm69a10: {
      if (!driver.status().rm69a10.init_flag ||
          driver.chip().rm69a10 == nullptr) {
        return false;
      }
      const uint8_t brightness =
          static_cast<uint8_t>((static_cast<uint16_t>(percent) * 255) / 100);
      if (!driver.chip().rm69a10->SetBrightness(brightness)) {
        return false;
      }
      break;
    }
    default:
      return false;
  }

  g_brightness_handle.percent = percent;
  return true;
}

}  // namespace

int PowerControlInit(void* config, int cfg_size, void** device_handle) {
  ESP_RETURN_ON_FALSE(device_handle != nullptr, ESP_ERR_INVALID_ARG, kTag,
      "device_handle is NULL");

  ESP_RETURN_ON_FALSE(config != nullptr, ESP_ERR_INVALID_ARG, kTag,
      "%s config is NULL", kPowerCtrlDeviceName);
  ESP_RETURN_ON_FALSE(
      cfg_size >= static_cast<int>(sizeof(dev_custom_power_ctrl_config_t)),
      ESP_ERR_INVALID_ARG, kTag, "%s config size is invalid",
      kPowerCtrlDeviceName);

  const auto* power_config =
      static_cast<const dev_custom_power_ctrl_config_t*>(config);
  ESP_RETURN_ON_FALSE(power_config->peripheral_name != nullptr,
      ESP_ERR_INVALID_ARG, kTag, "%s I2C peripheral name is NULL",
      kPowerCtrlDeviceName);
  const char* i2c_periph_name = power_config->peripheral_name;

  TDisplayP4Driver& driver = TDisplayP4Driver::GetInstance();

  if (!g_power_initialized) {
    ESP_RETURN_ON_ERROR(BindXl9535ToBoardI2c(i2c_periph_name), kTag,
        "BindXl9535ToBoardI2c failed");
    const esp_err_t ret = InitPowerControlDevice(driver);
    if (ret != ESP_OK && driver.chip().xl9535 != nullptr) {
      driver.chip().xl9535->Deinit(false);
    }
    if (ret != ESP_OK) {
      return ret;
    }
    g_power_initialized = true;
  }

  *device_handle = &driver;
  return ESP_OK;
}

int PowerControlDeinit(void*) {
  g_power_initialized = false;
  return ESP_OK;
}

int DisplayLcdInit(void*, int, void** device_handle) {
  ESP_RETURN_ON_FALSE(device_handle != nullptr, ESP_ERR_INVALID_ARG, kTag,
      "device_handle is NULL");
  ESP_RETURN_ON_FALSE(g_power_initialized, ESP_ERR_INVALID_STATE, kTag,
      "%s must be initialized before %s", kPowerCtrlDeviceName,
      kDisplayLcdDeviceName);

  ESP_RETURN_ON_FALSE(g_driver_objects_created, ESP_ERR_INVALID_STATE, kTag,
      "%s driver must be created before %s", kPowerCtrlDeviceName,
      kDisplayLcdDeviceName);
  TDisplayP4Driver& driver = TDisplayP4Driver::GetInstance();

  if (g_screen_handles.panel_handle == nullptr) {
    ESP_RETURN_ON_FALSE(
        driver.InitScreen(), ESP_FAIL, kTag, "InitScreen failed");
    ESP_RETURN_ON_FALSE(UpdateDisplayHandles(driver), ESP_FAIL, kTag,
        "UpdateDisplayHandles failed");

    g_screen_config = MakeDisplayConfig(driver.screen_info());
    const esp_err_t ret =
        esp_board_device_override_config(kDisplayLcdDeviceName,
            &g_screen_config, sizeof(g_screen_config));
    ESP_RETURN_ON_ERROR(ret, kTag, "esp_board_device_override_config failed");
  }

  *device_handle = &g_screen_handles;
  return ESP_OK;
}

int DisplayLcdDeinit(void*) {
  TDisplayP4Driver& driver = TDisplayP4Driver::GetInstance();

  ESP_RETURN_ON_FALSE(
      DeinitBrightness(driver), ESP_FAIL, kTag, "DeinitBrightness failed");
  ESP_RETURN_ON_FALSE(
      driver.DeinitScreen(), ESP_FAIL, kTag, "DeinitScreen failed");
  esp_board_device_restore_config(kDisplayLcdDeviceName);
  g_screen_handles = {};
  g_screen_config = {};
  return ESP_OK;
}

int LcdBrightnessInit(void*, int, void** device_handle) {
  ESP_RETURN_ON_FALSE(device_handle != nullptr, ESP_ERR_INVALID_ARG, kTag,
      "device_handle is NULL");

  TDisplayP4Driver& driver = TDisplayP4Driver::GetInstance();
  ESP_RETURN_ON_FALSE(g_screen_handles.panel_handle != nullptr,
      ESP_ERR_INVALID_STATE, kTag,
      "%s must be initialized before %s", kDisplayLcdDeviceName,
      kLcdBrightnessDeviceName);

  if (!g_brightness_initialized) {
    ESP_RETURN_ON_FALSE(driver.InitScreenBacklight(), ESP_FAIL, kTag,
        "InitScreenBacklight failed");
    ESP_RETURN_ON_FALSE(SetBrightnessPercent(kDefaultBrightnessPercent),
        ESP_FAIL, kTag, "SetBrightnessPercent failed");
    g_brightness_initialized = true;
  }

  *device_handle = &g_brightness_handle;
  return ESP_OK;
}

int LcdBrightnessDeinit(void*) {
  ESP_RETURN_ON_FALSE(DeinitBrightness(TDisplayP4Driver::GetInstance()),
      ESP_FAIL, kTag, "DeinitBrightness failed");
  return ESP_OK;
}

}  // namespace esp_claw_lilygo_t_display_p4

CUSTOM_DEVICE_IMPLEMENT(power_ctrl,
    esp_claw_lilygo_t_display_p4::PowerControlInit,
    esp_claw_lilygo_t_display_p4::PowerControlDeinit);
CUSTOM_DEVICE_IMPLEMENT(display_lcd,
    esp_claw_lilygo_t_display_p4::DisplayLcdInit,
    esp_claw_lilygo_t_display_p4::DisplayLcdDeinit);
CUSTOM_DEVICE_IMPLEMENT(lcd_brightness,
    esp_claw_lilygo_t_display_p4::LcdBrightnessInit,
    esp_claw_lilygo_t_display_p4::LcdBrightnessDeinit);
