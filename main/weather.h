#ifndef WEATHER_H
#define WEATHER_H

#include <stdint.h>

typedef struct {
    float temperature;
    uint32_t pressure; 
    float altitude;
    int angle;
    float x, y, z;
} weather_data_t;

extern volatile weather_data_t weather_data;

#define I2C_PIN_SDA 21
#define I2C_PIN_SCL 22
#define REFERENCE_PRESSURE 101325l

#endif /* WEATHER_H */