/* Persistent Sockets Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_check.h"
#include "bmp180.h"
#include "hmc5883l.h"
#include "math.h"

#include "sdkconfig.h"
#include "esp_http_server.h"
#include "weather.h"

extern void wifi_init_sta(void);
extern httpd_handle_t start_webserver(void);

#define LED_GPIO 2

volatile weather_data_t weather_data;

static const char *TAG = "weather1";

void bmp180_task(void *pvParameter)
{
    static const char *TAG = "BMP180 I2C Read";
    weather_data_t *weather_data = (weather_data_t *) pvParameter;

    bmp180_dev_t dev;
    memset(&dev, 0, sizeof(bmp180_dev_t));

    ESP_ERROR_CHECK(bmp180_init_desc(&dev, 0, I2C_PIN_SDA, I2C_PIN_SCL));
    ESP_ERROR_CHECK(bmp180_init(&dev));

    while(1) {
        esp_err_t err;

        err = bmp180_measure(&dev, &weather_data->temperature, &weather_data->pressure, BMP180_MODE_ULTRA_HIGH_RESOLUTION);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Reading of pressure from BMP180 failed, err = %d", err);
        }

        weather_data->altitude =  44330 * (1.0 - powf(weather_data->pressure / (float) REFERENCE_PRESSURE, 0.190295));
        ESP_LOGI(TAG, "Pressure %lu Pa, Altitude %.1f m, Temperature : %.1f degC",
                 weather_data->pressure, weather_data->altitude, weather_data->temperature);

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void hmc5883l_task(void *pvParameter)
{
    static const char *TAG = "HMC5883L I2C Read";
    weather_data_t *weather_data = (weather_data_t *) pvParameter;

    hmc5883l_dev_t dev;
    memset(&dev, 0, sizeof(hmc5883l_dev_t));
    
    ESP_ERROR_CHECK(hmc5883l_init_desc(&dev, 0, I2C_PIN_SDA, I2C_PIN_SCL));
    ESP_ERROR_CHECK(hmc5883l_init(&dev));
    ESP_ERROR_CHECK(hmc5883l_set_opmode(&dev, HMC5883L_MODE_SINGLE));
    ESP_ERROR_CHECK(hmc5883l_set_samples_averaged(&dev, HMC5883L_SAMPLES_8));
    ESP_ERROR_CHECK(hmc5883l_set_data_rate(&dev, HMC5883L_DATA_RATE_07_50));
    ESP_ERROR_CHECK(hmc5883l_set_gain(&dev, HMC5883L_GAIN_1090));   

    while(1) {
        esp_err_t err;
        hmc5883l_data_t data;

        err = hmc5883l_get_data(&dev, &data);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Reading of data from HMC5883L failed, err = %d", err);
        }

        weather_data->x = data.x;
        weather_data->y = data.y;
        weather_data->z = data.z;
        weather_data->angle = atan2(data.y, data.x) * (180 / 3.14159265) + 180;
        ESP_LOGI(TAG, "angle: %d, x: %f, y: %f, z: %f", weather_data->angle, data.x, data.y, data.z);

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}


static void configure_led(void)
{
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_INPUT_OUTPUT);
}
static void blink_led(void)
{
    gpio_set_level(LED_GPIO, !gpio_get_level(LED_GPIO));
}



void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(i2cdev_init());
    configure_led();

    wifi_init_sta();
    start_webserver();

    xTaskCreate(&bmp180_task, "bmp180_task", 1024*4, (void *)&weather_data, 5, NULL);
    xTaskCreate(&hmc5883l_task, "hmc5883l_task", 1024*4, (void *)&weather_data, 5, NULL);

    ESP_LOGI(TAG, "End of initialization.");

    while (1) {
        blink_led();
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

}
