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

// Add new message types and structures
typedef enum {
    MSG_BMP180_DATA,
    MSG_HMC5883L_DATA
} sensor_msg_type_t;

typedef struct {
    sensor_msg_type_t type;
    union {
        struct {
            float temperature;
            uint32_t pressure;
            float altitude;
        } bmp180;
        struct {
            int heading;
            float x;
            float y;
            float z;
        } hmc5883l;
    } data;
} sensor_message_t;

#define LED_GPIO 2
#define I2C_PIN_SDA 21
#define I2C_PIN_SCL 22
#define REFERENCE_PRESSURE 101325l

// Globals used for inter-task communication here - don't judge
extern volatile weather_data_t weather_data;
extern httpd_handle_t server;
extern int client_fd;

void wifi_init_sta(void);
httpd_handle_t start_webserver(void);
esp_err_t send_sensor_data(sensor_message_t *msg);


#endif /* WEATHER_H */