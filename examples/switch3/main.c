#include <stdio.h>
#include <FreeRTOS.h>
#include <esp/uart.h>
#include <esp8266.h>
#include <esplibs/libmain.h>
#include <espressif/esp_common.h>
#include <espressif/esp_sta.h>
#include <espressif/esp_wifi.h>
#include <etstimer.h>
#include <homekit/characteristics.h>
#include <homekit/homekit.h>
#include <stdio.h>
#include <task.h>
#include <wifi_config.h>
#include "sensor.h"
#include "wifi.h"
#ifndef WIFI_SSID
#error WIFI_SSID is not specified
#endif

const int relay_gpio = 0;
const int sensor_gpio = 2;

static void wifi_init() {
  struct sdk_station_config wifi_config = {
      .ssid = WIFI_SSID,
      .password = WIFI_PASSWORD,
  };

  sdk_wifi_set_opmode(STATION_MODE);
  sdk_wifi_station_set_config(&wifi_config);
  sdk_wifi_station_connect();
}

ETSTimer threshold;

void switch_on_callback(homekit_characteristic_t *_ch, homekit_value_t on,
                        void *context);

void relay_write(bool on) { gpio_write(relay_gpio, on ? 0 : 1); }

homekit_characteristic_t duration = HOMEKIT_CHARACTERISTIC_(SET_DURATION, 30);

homekit_characteristic_t switch_on = HOMEKIT_CHARACTERISTIC_(
    ON, false, .callback = HOMEKIT_CHARACTERISTIC_CALLBACK(switch_on_callback));

homekit_characteristic_t switch_auto = HOMEKIT_CHARACTERISTIC_(ON, false);

void target_state_setter(homekit_value_t value);

void switch_on_callback(homekit_characteristic_t *_ch, homekit_value_t on, void *context) {
  relay_write(switch_on.value.bool_value);
}

void my_accessory_identify(homekit_value_t _value) {
  printf("accessory identify\n");
}

homekit_characteristic_t occupancy_detected =
    HOMEKIT_CHARACTERISTIC_(OCCUPANCY_DETECTED, 0);

void notOccupied() {
  if (gpio_read(sensor_gpio) == 0) {
    sdk_os_timer_disarm(&threshold);
    occupancy_detected.value = HOMEKIT_UINT8(0);
    homekit_characteristic_notify(&occupancy_detected,
                                  occupancy_detected.value);
    if (switch_auto.value.bool_value) {
      if (switch_on.value.bool_value) {
        switch_on.value.bool_value = false;
        relay_write(switch_on.value.bool_value);
        homekit_characteristic_notify(&switch_on, switch_on.value);
      }
    }
  }
}

void occupied() {
  occupancy_detected.value = HOMEKIT_UINT8(1);
  homekit_characteristic_notify(&occupancy_detected, occupancy_detected.value);
}

/**
 * Contact Sensor Callback Function
 *
 * This function is called when a contact sensor detects a change in state.
 * It checks if the occupancy is detected and based on that, toggles the switch.
 * After that, it arms a timer to check again after a specific duration.
 */
void contact_sensor_callback(uint8_t gpio_num) {
  if (occupancy_detected.value.int_value == 0) {
    if (switch_auto.value.bool_value) {
      switch_on.value.bool_value = !switch_on.value.bool_value;
      relay_write(switch_on.value.bool_value);
      homekit_characteristic_notify(&switch_on, switch_on.value);
    }
    occupied();
  }
  sdk_os_timer_disarm(&threshold);
  sdk_os_timer_arm(&threshold, duration.value.int_value * 1000, 1);
}

homekit_characteristic_t name = HOMEKIT_CHARACTERISTIC_(NAME, "Switch");

homekit_accessory_t *accessories[] =
    {HOMEKIT_ACCESSORY(
             .id = 1, .category = homekit_accessory_category_lightbulb,
             .services =
                 (homekit_service_t *[]){
                     HOMEKIT_SERVICE(
                         ACCESSORY_INFORMATION,
                         .characteristics =
                             (homekit_characteristic_t *[]){
                                 &name,
                                 HOMEKIT_CHARACTERISTIC(MANUFACTURER, "Apfel"),
                                 HOMEKIT_CHARACTERISTIC(SERIAL_NUMBER, "1"),
                                 HOMEKIT_CHARACTERISTIC(MODEL, "A"),
                                 HOMEKIT_CHARACTERISTIC(FIRMWARE_REVISION,
                                                        "1.0"),
                                 HOMEKIT_CHARACTERISTIC(IDENTIFY,
                                                        my_accessory_identify),
                                 NULL}),
                     HOMEKIT_SERVICE(
                         LIGHTBULB, .primary = true,
                         .characteristics =
                             (homekit_characteristic_t *[]){
                                 HOMEKIT_CHARACTERISTIC(NAME, "Light"),
                                 &switch_on, NULL}),
                     HOMEKIT_SERVICE(SWITCH, .primary = false,
                                     .characteristics =
                                         (homekit_characteristic_t *[]){
                                             HOMEKIT_CHARACTERISTIC(
                                                 NAME, "Automatic"),
                                             &switch_auto, NULL}),
                     HOMEKIT_SERVICE(
                         OCCUPANCY_SENSOR, .primary = false,
                         .characteristics =
                             (homekit_characteristic_t *[]){
                                 HOMEKIT_CHARACTERISTIC(
                                     NAME, "Occupancy Sensor"),
                                 &occupancy_detected, &duration, NULL}),
                     NULL}),
     NULL};

homekit_server_config_t config = {
    .accessories = accessories,
    .password = "248-25-391",
    .setupId = "1AZ5",
};

void gpio_init() {
  if (sensor_create(sensor_gpio, contact_sensor_callback)) {
    printf("Failed to initialize sensor up\n");
  }
  gpio_enable(relay_gpio, GPIO_OUTPUT);
  relay_write(switch_on.value.bool_value);
}

void create_accessory_name() {
  uint8_t macaddr[6];
  sdk_wifi_get_macaddr(STATION_IF, macaddr);

  int name_len = snprintf(NULL, 0, "Switch-%02X%02X%02X", macaddr[3],
                          macaddr[4], macaddr[5]);
  char *name_value = malloc(name_len + 1);
  snprintf(name_value, name_len + 1, "Switch-%02X%02X%02X", macaddr[3],
           macaddr[4], macaddr[5]);

  name.value = HOMEKIT_STRING(name_value);
}

void user_init(void) {
  uart_set_baud(0, 115200);

  create_accessory_name();

  wifi_init();

  sdk_os_timer_setfn(&threshold, notOccupied, NULL);

  homekit_server_init(&config);

  gpio_init();
}
