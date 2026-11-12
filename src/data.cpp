/**
 * @file data.cpp
 * @brief Weather and time data (stub - to be implemented later)
 */
#include "data.h"
#include <Arduino.h>

void weather_get(void) {
    // TODO: implement weather display
}

void time_get(void) {
    configTime(28800, 0, "ntp1.aliyun.com", "ntp2.aliyun.com", "ntp3.aliyun.com");
}

void data_update(void) {
    // TODO: implement time/weather update
}
