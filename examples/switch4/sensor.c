#include <string.h>
#include <esplibs/libmain.h>
#include "sensor.h"

typedef struct _sensor {
    uint8_t gpio_num;
    sensor_callback_fn callback;

    uint16_t debounce_time;
    uint32_t last_event_time;

    struct _sensor *next;
} sensor_t;


sensor_t *sensors = NULL;


static sensor_t *sensor_find_by_gpio(const uint8_t gpio_num) {
    sensor_t *sensor = sensors;
    while (sensor && sensor->gpio_num != gpio_num)
        sensor = sensor->next;

    return sensor;
}


void sensor_intr_callback(uint8_t gpio) {
    sensor_t *sensor = sensor_find_by_gpio(gpio);
    if (!sensor)
        return;

    uint32_t now = xTaskGetTickCountFromISR();
    if ((now - sensor->last_event_time)*portTICK_PERIOD_MS < sensor->debounce_time) {
        // debounce time, ignore events
        return;
    }
    sensor->last_event_time = now;
    sensor->callback(sensor->gpio_num);
}

int sensor_create(const uint8_t gpio_num, sensor_callback_fn callback) {
    sensor_t *sensor = sensor_find_by_gpio(gpio_num);
    if (sensor)
        return -1;

    sensor = malloc(sizeof(sensor_t));
    memset(sensor, 0, sizeof(*sensor));
    sensor->gpio_num = gpio_num;
    sensor->callback = callback;

    // times in milliseconds
    sensor->debounce_time = 20;

    uint32_t now = xTaskGetTickCountFromISR();
    sensor->last_event_time = now;

    sensor->next = sensors;
    sensors = sensor;

    gpio_set_pullup(sensor->gpio_num, false, true);
    gpio_set_interrupt(sensor->gpio_num, GPIO_INTTYPE_EDGE_POS, sensor_intr_callback);

    return 0;
}


void sensor_delete(const uint8_t gpio_num) {
    if (!sensors)
        return;

    sensor_t *sensor = NULL;
    if (sensors->gpio_num == gpio_num) {
        sensor = sensors;
        sensors = sensors->next;
    } else {
        sensor_t *b = sensors;
        while (b->next) {
            if (b->next->gpio_num == gpio_num) {
                sensor = b->next;
                b->next = b->next->next;
                break;
            }
        }
    }

    if (sensor) {
        gpio_set_interrupt(gpio_num, GPIO_INTTYPE_EDGE_ANY, NULL);
    }
}

