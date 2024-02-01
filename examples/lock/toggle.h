#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    CONTACT_CLOSED,
    CONTACT_OPEN
} contact_sensor_state_t;

typedef void (*toggle_callback_fn)(contact_sensor_state_t);

int toggle_create(uint8_t gpio_num, toggle_callback_fn callback);
void toggle_delete(uint8_t gpio_num);
