#pragma once

typedef void (*sensor_callback_fn)(uint8_t gpio_num);

/** 
    Starts monitoring the given GPIO pin for the pressed value. Events are recieved through the callback.

    @param gpio_num The GPIO pin that should be monitored
    @param callback The callback that is called when an "sensor" event occurs.
    @return A negative integer if this method fails.
*/
int sensor_create(uint8_t gpio_num, sensor_callback_fn callback);

/** 
    Removes the given GPIO pin from monitoring.

    @param gpio_num The GPIO pin that should be removed from monitoring
*/
void sensor_delete(uint8_t gpio_num);
