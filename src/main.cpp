#include <Arduino.h>
#include <cstdio>
#include <DHTesp.h>
#include <LovyanGFX.hpp>
#include <lvgl.h>

namespace Pins {
constexpr uint8_t dht = 22;
constexpr int8_t backlight = 21;
}

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 panel;
  lgfx::Bus_SPI bus;
  lgfx::Touch_XPT2046 touch;

 public:
  LGFX() {
    auto busConfig = bus.config();
    busConfig.spi_host = SPI2_HOST;
    busConfig.spi_mode = 0;
    busConfig.freq_write = 20000000;
    busConfig.freq_read = 10000000;
    busConfig.pin_sclk = 14;
    busConfig.pin_mosi = 13;
    busConfig.pin_miso = 12;
    busConfig.pin_dc = 2;
    bus.config(busConfig);
    panel.setBus(&bus);

    auto panelConfig = panel.config();
    panelConfig.pin_cs = 15;
    panelConfig.pin_rst = -1;
    panelConfig.pin_busy = -1;
    panelConfig.panel_width = 240;
    panelConfig.panel_height = 320;
    panelConfig.memory_width = 240;
    panelConfig.memory_height = 320;
    panelConfig.readable = false;
    panelConfig.invert = false;
    panelConfig.rgb_order = false;
    panelConfig.bus_shared = false;
    panel.config(panelConfig);

    auto touchConfig = touch.config();
    touchConfig.spi_host = -1;
    touchConfig.freq = 1000000;
    touchConfig.pin_sclk = 25;
    touchConfig.pin_mosi = 32;
    touchConfig.pin_miso = 39;
    touchConfig.pin_cs = 33;
    touchConfig.pin_int = -1;
    touchConfig.bus_shared = false;
    touchConfig.x_min = 300;
    touchConfig.x_max = 3900;
    touchConfig.y_min = 3700;
    touchConfig.y_max = 200;
    touch.config(touchConfig);

    panel.setTouch(&touch);
    setPanel(&panel);
  }
};

LGFX display;
DHTesp dhtSensor;
SemaphoreHandle_t sensorMutex;
TaskHandle_t dhtTaskHandle = nullptr;
TaskHandle_t uiTaskHandle = nullptr;

constexpr TickType_t sensorMutexTimeout = 0;

struct SensorData {
  float temperature = NAN;
  float humidity = NAN;
  bool valid = false;
  bool enabled = true;
  uint32_t sequence = 0;
};

SensorData latestSensorData;
bool sensorEnabled = true;

namespace Ui {
constexpr uint16_t screenWidth = 320;
constexpr uint16_t screenHeight = 240;
constexpr uint16_t drawBufferLines = 20;
constexpr unsigned long chartSampleIntervalMs = 2000;
}

lv_disp_draw_buf_t drawBuffer;
lv_color_t drawPixels[Ui::screenWidth * Ui::drawBufferLines];
lv_obj_t *temperatureArc = nullptr;
lv_obj_t *humidityArc = nullptr;
lv_obj_t *temperatureValue = nullptr;
lv_obj_t *humidityValue = nullptr;
lv_obj_t *statusLabel = nullptr;
lv_obj_t *sensorStateLabel = nullptr;
lv_obj_t *sensorSwitch = nullptr;
lv_obj_t *chart = nullptr;
lv_chart_series_t *temperatureSeries = nullptr;
lv_chart_series_t *humiditySeries = nullptr;

void setLabelText(lv_obj_t *label, const char *text) {
  if (label != nullptr && text != nullptr) {
    lv_label_set_text(label, text);
  }
}

void setSensorValueLabels(float temperature, float humidity) {
  if (temperatureValue == nullptr || humidityValue == nullptr) {
    return;
  }

  char temperatureText[20];
  char humidityText[20];
  const int temperatureTenths = static_cast<int>(roundf(temperature * 10.0f));
  const int humidityTenths = static_cast<int>(roundf(humidity * 10.0f));

  snprintf(temperatureText, sizeof(temperatureText), "%d.%d C",
           temperatureTenths / 10, abs(temperatureTenths % 10));
  snprintf(humidityText, sizeof(humidityText), "%d.%d %%",
           humidityTenths / 10, abs(humidityTenths % 10));

  lv_label_set_text(temperatureValue, temperatureText);
  lv_label_set_text(humidityValue, humidityText);
}

void setDashboardState(const SensorData &data) {
  if (!data.enabled) {
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x263238), LV_PART_MAIN);
    lv_obj_set_style_text_color(statusLabel, lv_color_hex(0xCFD8DC), 0);
    setLabelText(statusLabel, "SENSOR PAUSED");
    setLabelText(temperatureValue, "--.- C");
    setLabelText(humidityValue, "--.- %");
    setLabelText(sensorStateLabel, "DHT11 OFF");
    return;
  }

  const bool critical = data.valid && data.temperature > 35.0f;
  const lv_color_t background = critical ? lv_color_hex(0x9E241C)
                                         : lv_color_hex(0x123B3A);
  const lv_color_t foreground = critical ? lv_color_hex(0xFFB4AB)
                                         : lv_color_hex(0xA8F0D7);

  lv_obj_set_style_bg_color(lv_scr_act(), background, LV_PART_MAIN);
  lv_obj_set_style_text_color(statusLabel, foreground, 0);
  lv_obj_set_style_arc_color(temperatureArc, foreground, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(humidityArc, lv_color_hex(0x65B7FF), LV_PART_INDICATOR);

  if (!data.valid) {
    setLabelText(statusLabel, "DHT11 SENSOR ERROR");
    setLabelText(temperatureValue, "--.- C");
    setLabelText(humidityValue, "--.- %");
    setLabelText(sensorStateLabel, "DHT11 ON");
    return;
  }

  setSensorValueLabels(data.temperature, data.humidity);
  setLabelText(statusLabel, critical ? "CRITICAL: HIGH TEMPERATURE"
                                     : "GREENHOUSE CONDITIONS NORMAL");
  setLabelText(sensorStateLabel, "DHT11 ON");

  lv_arc_set_value(temperatureArc, constrain(static_cast<int>(data.temperature), 0, 50));
  lv_arc_set_value(humidityArc, constrain(static_cast<int>(data.humidity), 0, 100));
}

void updateChart(const SensorData &data) {
  if (!data.valid) {
    return;
  }

  lv_chart_set_next_value(chart, temperatureSeries,
                          static_cast<lv_coord_t>(constrain(data.temperature, 0.0f, 50.0f)));
  lv_chart_set_next_value(chart, humiditySeries,
                          static_cast<lv_coord_t>(constrain(data.humidity, 0.0f, 100.0f)));
  lv_chart_refresh(chart);
}

void dhtReadTask(void *parameter) {
  (void)parameter;
  dhtSensor.setup(Pins::dht, DHTesp::DHT11);
  TickType_t nextRead = xTaskGetTickCount();

  for (;;) {
    bool enabled = false;
    if (xSemaphoreTake(sensorMutex, sensorMutexTimeout) == pdTRUE) {
      enabled = sensorEnabled;
      xSemaphoreGive(sensorMutex);
    }

    if (!enabled) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    // DHT11 timing is blocking by nature; this call is isolated on Core 0.
    const TempAndHumidity reading = dhtSensor.getTempAndHumidity();
    SensorData nextData;
    nextData.enabled = true;
    nextData.valid = !isnan(reading.temperature) && !isnan(reading.humidity);
    nextData.temperature = reading.temperature;
    nextData.humidity = reading.humidity;

    if (xSemaphoreTake(sensorMutex, sensorMutexTimeout) == pdTRUE) {
      nextData.sequence = latestSensorData.sequence + 1;
      latestSensorData = nextData;
      xSemaphoreGive(sensorMutex);
    }

    vTaskDelayUntil(&nextRead, pdMS_TO_TICKS(2000));
  }
}

void lvglUiTask(void *parameter) {
  (void)parameter;
  SensorData displayedData;
  displayedData.enabled = true;
  unsigned long lastTick = millis();
  unsigned long lastChartSample = millis();

  for (;;) {
    const unsigned long now = millis();
    lv_tick_inc(now - lastTick);
    lastTick = now;

    if (xSemaphoreTake(sensorMutex, sensorMutexTimeout) == pdTRUE) {
      displayedData = latestSensorData;
      xSemaphoreGive(sensorMutex);
      setDashboardState(displayedData);
    }

    if (displayedData.enabled && displayedData.valid &&
        now - lastChartSample >= Ui::chartSampleIntervalMs) {
      updateChart(displayedData);
      lastChartSample = now;
    }

    lv_timer_handler();
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void sensorSwitchChanged(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) {
    return;
  }

  const bool enabled = lv_obj_has_state(sensorSwitch, LV_STATE_CHECKED);
  if (xSemaphoreTake(sensorMutex, sensorMutexTimeout) == pdTRUE) {
    sensorEnabled = enabled;
    latestSensorData.enabled = enabled;
    xSemaphoreGive(sensorMutex);
  }
}

void flushDisplay(lv_disp_drv_t *driver, const lv_area_t *area,
                  lv_color_t *colorMap) {
  const uint32_t width = area->x2 - area->x1 + 1;
  const uint32_t height = area->y2 - area->y1 + 1;

  display.startWrite();
  display.setAddrWindow(area->x1, area->y1, width, height);
  display.pushPixels(reinterpret_cast<uint16_t *>(&colorMap->full), width * height);
  display.endWrite();
  lv_disp_flush_ready(driver);
}

void touchRead(lv_indev_drv_t *driver, lv_indev_data_t *data) {
  uint16_t x = 0;
  uint16_t y = 0;
  const bool touched = display.getTouch(&x, &y);

  data->state = touched ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
  if (touched) {
    data->point.x = static_cast<lv_coord_t>(constrain(x, 0, Ui::screenWidth - 1));
    data->point.y = static_cast<lv_coord_t>(
        Ui::screenHeight - 1 - constrain(y, 0, Ui::screenHeight - 1));
  }
}

lv_obj_t *createArc(lv_coord_t x, lv_color_t color) {
  lv_obj_t *arc = lv_arc_create(lv_scr_act());
  lv_obj_set_size(arc, 88, 88);
  lv_obj_align(arc, LV_ALIGN_TOP_LEFT, x, 28);
  lv_arc_set_range(arc, 0, 100);
  lv_arc_set_bg_angles(arc, 135, 405);
  lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
  lv_obj_set_style_arc_width(arc, 10, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, 10, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, lv_color_hex(0x315B59), LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
  lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
  return arc;
}

void createDashboard() {
  lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_pad_all(lv_scr_act(), 0, LV_PART_MAIN);

  temperatureArc = createArc(20, lv_color_hex(0xFFB45B));
  humidityArc = createArc(124, lv_color_hex(0x65B7FF));

  temperatureValue = lv_label_create(lv_scr_act());
  lv_obj_set_style_text_color(temperatureValue, lv_color_white(), 0);
  lv_obj_align(temperatureValue, LV_ALIGN_TOP_LEFT, 30, 62);

  humidityValue = lv_label_create(lv_scr_act());
  lv_obj_set_style_text_color(humidityValue, lv_color_white(), 0);
  lv_obj_align(humidityValue, LV_ALIGN_TOP_LEFT, 137, 62);

  lv_obj_t *tempTitle = lv_label_create(lv_scr_act());
  lv_label_set_text(tempTitle, "TEMP");
  lv_obj_set_style_text_color(tempTitle, lv_color_hex(0xFFB45B), 0);
  lv_obj_align(tempTitle, LV_ALIGN_TOP_LEFT, 42, 105);

  lv_obj_t *humTitle = lv_label_create(lv_scr_act());
  lv_label_set_text(humTitle, "HUM");
  lv_obj_set_style_text_color(humTitle, lv_color_hex(0x65B7FF), 0);
  lv_obj_align(humTitle, LV_ALIGN_TOP_LEFT, 149, 105);

  statusLabel = lv_label_create(lv_scr_act());
  lv_obj_set_style_text_color(statusLabel, lv_color_hex(0xA8F0D7), 0);
  lv_obj_align(statusLabel, LV_ALIGN_TOP_MID, 38, 8);

  chart = lv_chart_create(lv_scr_act());
  lv_obj_set_size(chart, 296, 68);
  lv_obj_align(chart, LV_ALIGN_TOP_LEFT, 12, 126);
  lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
  lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
  lv_chart_set_div_line_count(chart, 3, 4);
  lv_obj_set_style_bg_color(chart, lv_color_hex(0x0C2928), LV_PART_MAIN);
  lv_obj_set_style_border_width(chart, 0, LV_PART_MAIN);
  temperatureSeries = lv_chart_add_series(chart, lv_color_hex(0xFFB45B), LV_CHART_AXIS_PRIMARY_Y);
  humiditySeries = lv_chart_add_series(chart, lv_color_hex(0x65B7FF), LV_CHART_AXIS_PRIMARY_Y);
  lv_chart_set_point_count(chart, 24);

  sensorStateLabel = lv_label_create(lv_scr_act());
  lv_obj_set_style_text_color(sensorStateLabel, lv_color_hex(0xA8F0D7), 0);
  lv_obj_align(sensorStateLabel, LV_ALIGN_BOTTOM_LEFT, 14, -13);

  sensorSwitch = lv_switch_create(lv_scr_act());
  lv_obj_align(sensorSwitch, LV_ALIGN_BOTTOM_RIGHT, -14, -8);
  lv_obj_add_state(sensorSwitch, LV_STATE_CHECKED);
  lv_obj_add_event_cb(sensorSwitch, sensorSwitchChanged,
                      LV_EVENT_VALUE_CHANGED, nullptr);

  SensorData initialData;
  initialData.temperature = NAN;
  initialData.humidity = NAN;
  initialData.valid = false;
  initialData.enabled = true;
  updateChart(initialData);
  setDashboardState(initialData);
}

void setup() {
  pinMode(Pins::backlight, OUTPUT);
  digitalWrite(Pins::backlight, HIGH);

  display.init();
  display.setRotation(1);
  display.setColorDepth(16);
  display.setSwapBytes(true);
  display.setBrightness(255);

  lv_init();
  lv_disp_draw_buf_init(&drawBuffer, drawPixels, nullptr,
                        Ui::screenWidth * Ui::drawBufferLines);

  static lv_disp_drv_t displayDriver;
  lv_disp_drv_init(&displayDriver);
  displayDriver.hor_res = Ui::screenWidth;
  displayDriver.ver_res = Ui::screenHeight;
  displayDriver.flush_cb = flushDisplay;
  displayDriver.draw_buf = &drawBuffer;
  lv_disp_drv_register(&displayDriver);

  static lv_indev_drv_t touchDriver;
  lv_indev_drv_init(&touchDriver);
  touchDriver.type = LV_INDEV_TYPE_POINTER;
  touchDriver.read_cb = touchRead;
  lv_indev_drv_register(&touchDriver);

  sensorMutex = xSemaphoreCreateMutex();
  if (sensorMutex == nullptr) {
    while (true) {
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }
  createDashboard();

  xTaskCreatePinnedToCore(dhtReadTask, "DHT11 readings", 4096, nullptr, 1,
                          &dhtTaskHandle, 0);
  xTaskCreatePinnedToCore(lvglUiTask, "LVGL dashboard", 8192, nullptr, 1,
                          &uiTaskHandle, 1);
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
