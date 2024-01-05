/*
 * Implemenetation of lock mechanism accessory for a magnet lock.
 * When unlocked, it changes relay state (unlocks) for configured period and then
 * changes it back.
 */

#include <stdio.h>
#include <espressif/esp_wifi.h>
#include <espressif/esp_sta.h>
#include <espressif/esp_common.h>
#include <esp/uart.h>
#include <esp8266.h>
#include <FreeRTOS.h>
#include <task.h>
#include <etstimer.h>
#include <esplibs/libmain.h>
#include "contact_sensor.h"
#include <homekit/homekit.h>
#include <homekit/characteristics.h>
#include <wifi_config.h>
#include "wifi.h"
#ifndef REED_PIN
#error REED_PIN is not specified
#endif

#ifndef WIFI_SSID
#error WIFI_SSID is not specified
#endif

// The GPIO pin that is connected to a relay
const int relay_gpio = 0;
// Which signal to send to relay to open the lock (0 or 1)
const int relay_open_signal = 0;

void lock_lock();
void lock_unlock();
void lock_timeout();

void relay_write(int value) {
    gpio_write(relay_gpio, value ? 1 : 0);    
}

static void wifi_init() {
    struct sdk_station_config wifi_config = {
        .ssid = WIFI_SSID,
        .password = WIFI_PASSWORD,
    };

    sdk_wifi_set_opmode(STATION_MODE);
    sdk_wifi_station_set_config(&wifi_config);
    sdk_wifi_station_connect();
}

homekit_value_t door_state_getter() {
    printf("Door state was requested (%s).\n", contact_sensor_state_get(REED_PIN) == CONTACT_OPEN ? "open": "closed");
    return HOMEKIT_UINT8(contact_sensor_state_get(REED_PIN) == CONTACT_OPEN ? 1 : 0);
}

homekit_characteristic_t door_open_characteristic = HOMEKIT_CHARACTERISTIC_(CONTACT_SENSOR_STATE, 0,
    .getter=door_state_getter,
    .setter=NULL,
    NULL
);

typedef enum {
    lock_state_unsecured = 0,
    lock_state_secured = 1,
    lock_state_jammed = 2,
    lock_state_unknown = 3,
} lock_state_t;

homekit_characteristic_t name = HOMEKIT_CHARACTERISTIC_(NAME, "Lock");

homekit_characteristic_t lock_current_state = HOMEKIT_CHARACTERISTIC_(
    LOCK_CURRENT_STATE,
    lock_state_unknown,
);

void lock_timeout_setter(homekit_value_t value);
homekit_value_t lock_timeout_get();

homekit_characteristic_t lock_timeout_state = HOMEKIT_CHARACTERISTIC_(
    LOCK_MANAGEMENT_AUTO_SECURITY_TIMEOUT,
    60,
    .getter=lock_timeout_get,
    .setter=lock_timeout_setter
);

homekit_value_t lock_timeout_get() {
    return lock_timeout_state.value;
}

void lock_timeout_setter(homekit_value_t value) {
    lock_timeout_state.value = value;
}

void lock_target_state_setter(homekit_value_t value);

homekit_characteristic_t lock_target_state = HOMEKIT_CHARACTERISTIC_(
    LOCK_TARGET_STATE,
    lock_state_secured,
    .setter=lock_target_state_setter,
);

void contact_sensor_callback(uint8_t gpio, contact_sensor_state_t state) {
    switch (state) {
        case CONTACT_OPEN:
            if (lock_current_state.value.int_value == lock_state_jammed) {
                lock_current_state.value = HOMEKIT_UINT8(lock_state_secured);
                homekit_characteristic_notify(&lock_current_state, lock_current_state.value);
            }
        case CONTACT_CLOSED:
            if (lock_current_state.value.int_value == lock_state_unknown) {
                if (lock_target_state.value.int_value != lock_state_secured) {
                    lock_target_state.value = HOMEKIT_UINT8(lock_state_secured);
                    homekit_characteristic_notify(&lock_target_state, lock_target_state.value);
                }
                lock_current_state.value = HOMEKIT_UINT8(lock_state_secured);
                homekit_characteristic_notify(&lock_current_state, lock_current_state.value);
            }
            printf("Pushing contact sensor state '%s'.\n", state == CONTACT_OPEN ? "open" : "closed");
            homekit_characteristic_notify(&door_open_characteristic, door_state_getter());
            break;
        default:
            printf("Unknown contact sensor event: %d\n", state);
    }
}

void gpio_init() {
    if (contact_sensor_create(REED_PIN, contact_sensor_callback)) {
        printf("Failed to initialize door\n");
    }
    gpio_enable(relay_gpio, GPIO_OUTPUT);
    relay_write(!relay_open_signal);
}

void lock_identify(homekit_value_t _value) {
    printf("Lock identify\n");
}

void lock_control_point(homekit_value_t value) {
    // Nothing to do here
}

ETSTimer door_timer;
ETSTimer lock_timer;

void lock_target_state_setter(homekit_value_t value) {
    lock_target_state.value = value;
    
    if (value.int_value == 0) {
        if (contact_sensor_state_get(REED_PIN) == CONTACT_OPEN) {
            lock_current_state.value = HOMEKIT_UINT8(lock_state_unknown);
            homekit_characteristic_notify(&lock_current_state, lock_current_state.value);
            sdk_os_timer_disarm(&lock_timer);
            sdk_os_timer_disarm(&door_timer);
        } else {
            lock_unlock();
        }
    } else {
        lock_lock();
    }
}

void lock_lock() {
    sdk_os_timer_disarm(&lock_timer);
    sdk_os_timer_disarm(&door_timer);
    
    if (lock_current_state.value.int_value == lock_state_unsecured) {
        lock_current_state.value = HOMEKIT_UINT8(lock_state_secured);
        homekit_characteristic_notify(&lock_current_state, lock_current_state.value);
    }
    
    relay_write(!relay_open_signal);
}

void lock_timeout() {
    if (lock_target_state.value.int_value != lock_state_secured) {
        lock_target_state.value = HOMEKIT_UINT8(lock_state_secured);
        homekit_characteristic_notify(&lock_target_state, lock_target_state.value);
    }

    if (contact_sensor_state_get(REED_PIN) != CONTACT_OPEN) {
        lock_current_state.value = HOMEKIT_UINT8(lock_state_jammed);
        homekit_characteristic_notify(&lock_current_state, lock_current_state.value);
    }
    
    lock_lock();
}

void door_timeout() {
    if (contact_sensor_state_get(REED_PIN) == CONTACT_OPEN) {
        sdk_os_timer_disarm(&door_timer);
        lock_timeout();
    }
}

void lock_init() {
    lock_current_state.value = HOMEKIT_UINT8(lock_state_secured);
    homekit_characteristic_notify(&lock_current_state, lock_current_state.value);
    homekit_characteristic_notify(&door_open_characteristic, door_state_getter());
    sdk_os_timer_disarm(&lock_timer);
    sdk_os_timer_setfn(&lock_timer, lock_timeout, NULL);
    sdk_os_timer_disarm(&door_timer);
    sdk_os_timer_setfn(&door_timer, door_timeout, NULL);
}

void lock_unlock() {
    sdk_os_timer_disarm(&lock_timer);
    sdk_os_timer_disarm(&door_timer);
    
    lock_current_state.value = HOMEKIT_UINT8(lock_state_unsecured);
    homekit_characteristic_notify(&lock_current_state, lock_current_state.value);
    
    relay_write(relay_open_signal);

    sdk_os_timer_arm(&lock_timer, lock_timeout_state.value.int_value * 1000, 0);
    sdk_os_timer_arm(&door_timer, 500, 1);
}

homekit_accessory_t *accessories[] = {
    HOMEKIT_ACCESSORY(.id=1, .category=homekit_accessory_category_door_lock, .services=(homekit_service_t*[]){
        HOMEKIT_SERVICE(ACCESSORY_INFORMATION, .characteristics=(homekit_characteristic_t*[]){
            &name,
            HOMEKIT_CHARACTERISTIC(MANUFACTURER, "Apfel"),
            HOMEKIT_CHARACTERISTIC(SERIAL_NUMBER, "1"),
            HOMEKIT_CHARACTERISTIC(MODEL, "A"),
            HOMEKIT_CHARACTERISTIC(FIRMWARE_REVISION, "1.0"),
            HOMEKIT_CHARACTERISTIC(IDENTIFY, lock_identify),
            NULL
        }),
        HOMEKIT_SERVICE(LOCK_MECHANISM, .primary=true, .characteristics=(homekit_characteristic_t*[]){
            HOMEKIT_CHARACTERISTIC(NAME, "Türsummer"),
            &lock_current_state,
            &lock_target_state,
            NULL
        }),
        HOMEKIT_SERVICE(LOCK_MANAGEMENT, .characteristics=(homekit_characteristic_t*[]){
            HOMEKIT_CHARACTERISTIC(LOCK_CONTROL_POINT,
                .setter=lock_control_point,
                 NULL
            ),
            HOMEKIT_CHARACTERISTIC(LOCK_MANAGEMENT_AUTO_SECURITY_TIMEOUT,
               60,
               .setter=lock_timeout_setter,
               .getter=lock_timeout_get,
                NULL
            ),
            HOMEKIT_CHARACTERISTIC(VERSION, "1"),
            NULL
        }),
        HOMEKIT_SERVICE(CONTACT_SENSOR, .primary=false, .characteristics=(homekit_characteristic_t*[]){
                HOMEKIT_CHARACTERISTIC(NAME, "Magnetschalter"),
                &door_open_characteristic,
                NULL
            },
        ),
        NULL
    }),
    NULL
};

homekit_server_config_t config = {
    .accessories = accessories,
    .password = "742-84-519",
    .setupId = "2NS4",
};

void create_accessory_name() {
    uint8_t macaddr[6];
    sdk_wifi_get_macaddr(STATION_IF, macaddr);

    int name_len = snprintf(NULL, 0, "Tür-%02X%02X%02X",
                            macaddr[3], macaddr[4], macaddr[5]);
    char *name_value = malloc(name_len+1);
    
    snprintf(name_value, name_len+1, "Tür-%02X%02X%02X",
             macaddr[3], macaddr[4], macaddr[5]);

    name.value = HOMEKIT_STRING(name_value);
}

void user_init(void) {
    uart_set_baud(0, 115200);

    create_accessory_name();

    wifi_init();
    
    homekit_server_init(&config);
    
    gpio_init();
    
    lock_init();
}
