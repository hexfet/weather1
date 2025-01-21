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

#include <esp_http_server.h>

#include "sdkconfig.h"

extern void wifi_init_sta(void);

#define LED_GPIO 2

typedef struct {
    float temperature;
    uint32_t pressure;
    float altitude;
    int angle;
    float x, y, z;
} weather_data_t;

static volatile weather_data_t weather_data;

static const char *TAG = "weather1";

#define I2C_PIN_SDA 21
#define I2C_PIN_SCL 22
#define REFERENCE_PRESSURE 101325l

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

/* Function to free context */
static void adder_free_func(void *ctx)
{
    ESP_LOGI(TAG, "/adder Free Context function called");
    free(ctx);
}

/* This handler keeps accumulating data that is posted to it into a per
 * socket/session context. And returns the result.
 */
static esp_err_t weather_post_handler(httpd_req_t *req)
{
    /* Log total visitors */
    unsigned *visitors = (unsigned *)req->user_ctx;
    ESP_LOGI(TAG, "/ visitor count = %d", ++(*visitors));

    char buf[10];
    char outbuf[50];
    int  ret;

    /* Read data received in the request */
    ret = httpd_req_recv(req, buf, sizeof(buf));
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }

    buf[ret] = '\0';
    int val = atoi(buf);
    ESP_LOGI(TAG, "/ handler read %d", val);

    /* Create session's context if not already available */
    if (! req->sess_ctx) {
        ESP_LOGI(TAG, "/ allocating new session");
        req->sess_ctx = malloc(sizeof(int));
        ESP_RETURN_ON_FALSE(req->sess_ctx, ESP_ERR_NO_MEM, TAG, "Failed to allocate sess_ctx");
        req->free_ctx = adder_free_func;
        *(int *)req->sess_ctx = 0;
    }

    /* Add the received data to the context */
    int *adder = (int *)req->sess_ctx;
    *adder += val;

    /* Respond with the accumulated value */
    snprintf(outbuf, sizeof(outbuf),"%d", *adder);
    httpd_resp_send(req, outbuf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

#include "index.html.h"
#include "weather.css.h"

/* This handler gets the present value of the accumulator */
static esp_err_t weather_get_handler(httpd_req_t *req)
{
    /* Log total visitors */
    unsigned *visitors = (unsigned *)req->user_ctx;
    ESP_LOGI(TAG, "/ visitor count = %d", ++(*visitors));

    /* Create session's context if not already available */
    if (! req->sess_ctx) {
        ESP_LOGI(TAG, "/ GET allocating new session");
        req->sess_ctx = malloc(sizeof(int));
        ESP_RETURN_ON_FALSE(req->sess_ctx, ESP_ERR_NO_MEM, TAG, "Failed to allocate sess_ctx");
        req->free_ctx = adder_free_func;
        *(int *)req->sess_ctx = 0;
    }
    ESP_LOGI(TAG, "/ GET handler send index_page");

    if (strcmp(req->uri, "/weather.css") == 0) {
        httpd_resp_set_type(req, "text/css");
        httpd_resp_send(req, weather_css, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    uint32_t buf_size = strlen(index_html) + 100;
    char *buffer = malloc(buf_size);
    snprintf(buffer, buf_size, index_html, weather_data.temperature, (weather_data.temperature * 9 / 5)+32,
                                           weather_data.pressure, (float)weather_data.pressure / 3886.0,
                                           weather_data.altitude, weather_data.altitude * 3.281,
                                           weather_data.angle,
                                           weather_data.x,
                                           weather_data.y,
                                           weather_data.z
  );
    httpd_resp_send(req, buffer, HTTPD_RESP_USE_STRLEN);
    free(buffer);
    return ESP_OK;
}

#if CONFIG_EXAMPLE_SESSION_CTX_HANDLERS
// login and logout handler are created to test the server functionality to delete the older sess_ctx if it is changed from another handler.
// login handler creates a new sess_ctx
static esp_err_t login_handler(httpd_req_t *req)
{
    /* Log total visitors */
    unsigned *visitors = (unsigned *)req->user_ctx;
    ESP_LOGI(TAG, "/login visitor count = %d", ++(*visitors));

    char outbuf[50];

    /* Create session's context if not already available */
    if (! req->sess_ctx) {
        ESP_LOGI(TAG, "/login GET allocating new session");
        req->sess_ctx = malloc(sizeof(int));
        if (!req->sess_ctx) {
            return ESP_ERR_NO_MEM;
        }
        *(int *)req->sess_ctx = 1;
    }
    ESP_LOGI(TAG, "/login GET handler send %d", *(int *)req->sess_ctx);

    /* Respond with the accumulated value */
    snprintf(outbuf, sizeof(outbuf),"%d", *((int *)req->sess_ctx));
    httpd_resp_send(req, outbuf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// This handler sets sess_ctx to NULL.
static esp_err_t logout_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Logging out");
    // Setting sess_ctx to NULL here. This is done to test the server functionality to free the older sesss_ctx if it is changed by some handler.
    req->sess_ctx = NULL;
    char outbuf[50];
    snprintf(outbuf, sizeof(outbuf),"%d", 1);
    httpd_resp_send(req, outbuf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}
#endif // CONFIG_EXAMPLE_SESSION_CTX_HANDLERS

/* This handler resets the value of the accumulator */
static esp_err_t weather_put_handler(httpd_req_t *req)
{
    /* Log total visitors */
    unsigned *visitors = (unsigned *)req->user_ctx;
    ESP_LOGI(TAG, "/ visitor count = %d", ++(*visitors));

    char buf[10];
    char outbuf[50];
    int  ret;

    /* Read data received in the request */
    ret = httpd_req_recv(req, buf, sizeof(buf));
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }

    buf[ret] = '\0';
    int val = atoi(buf);
    ESP_LOGI(TAG, "/ PUT handler read %d", val);

    /* Create session's context if not already available */
    if (! req->sess_ctx) {
        ESP_LOGI(TAG, "/ PUT allocating new session");
        req->sess_ctx = malloc(sizeof(int));
        ESP_RETURN_ON_FALSE(req->sess_ctx, ESP_ERR_NO_MEM, TAG, "Failed to allocate sess_ctx");
        req->free_ctx = adder_free_func;
    }
    *(int *)req->sess_ctx = val;

    /* Respond with the reset value */
    snprintf(outbuf, sizeof(outbuf),"%d", *((int *)req->sess_ctx));
    httpd_resp_send(req, outbuf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* Maintain a variable which stores the number of times
 * the "/" URI has been visited */
static unsigned visitors = 0;

static const httpd_uri_t weather_post = {
    .uri      = "/",
    .method   = HTTP_POST,
    .handler  = weather_post_handler,
    .user_ctx = &visitors
};

static const httpd_uri_t weather_get = {
    .uri      = "/",
    .method   = HTTP_GET,
    .handler  = weather_get_handler,
    .user_ctx = &visitors
};
static const httpd_uri_t weather_get2 = {
    .uri      = "/index.html",
    .method   = HTTP_GET,
    .handler  = weather_get_handler,
    .user_ctx = &visitors
};
static const httpd_uri_t weather_get3 = {
    .uri      = "/weather.css",
    .method   = HTTP_GET,
    .handler  = weather_get_handler,
    .user_ctx = &visitors
};

#if CONFIG_EXAMPLE_SESSION_CTX_HANDLERS
static const httpd_uri_t login = {
    .uri      = "/login",
    .method   = HTTP_GET,
    .handler  = login_handler,
    .user_ctx = &visitors
};

static const httpd_uri_t logout = {
    .uri      = "/logout",
    .method   = HTTP_GET,
    .handler  = logout_handler,
    .user_ctx = &visitors
};
#endif // CONFIG_EXAMPLE_SESSION_CTX_HANDLERS

static const httpd_uri_t weather_put = {
    .uri      = "/",
    .method   = HTTP_PUT,
    .handler  = weather_put_handler,
    .user_ctx = &visitors
};

static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    httpd_handle_t server;

    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &weather_get);
        httpd_register_uri_handler(server, &weather_get2);
        httpd_register_uri_handler(server, &weather_get3);                
#if CONFIG_EXAMPLE_SESSION_CTX_HANDLERS
        httpd_register_uri_handler(server, &login);
        httpd_register_uri_handler(server, &logout);
#endif // CONFIG_EXAMPLE_SESSION_CTX_HANDLERS
        httpd_register_uri_handler(server, &weather_put);
        httpd_register_uri_handler(server, &weather_post);
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

static esp_err_t stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    return httpd_stop(server);
}


static void disconnect_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server) {
        ESP_LOGI(TAG, "Stopping webserver");
        if (stop_webserver(*server) == ESP_OK) {
            *server = NULL;
        } else {
            ESP_LOGE(TAG, "Failed to stop http server");
        }
    }
}

static void connect_handler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server == NULL) {
        ESP_LOGI(TAG, "Starting webserver");
        *server = start_webserver();
    }
}


void app_main(void)
{
    static httpd_handle_t server = NULL;

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(i2cdev_init());
    configure_led();

    wifi_init_sta();

    /* Register event handlers to stop the server when Wi-Fi or Ethernet is disconnected,
     * and re-start it upon connection.
     */
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &server));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &server));

    /* Start the server for the first time */
    server = start_webserver();

    xTaskCreate(&bmp180_task, "bmp180_task", 1024*4, &weather_data, 5, NULL);
    xTaskCreate(&hmc5883l_task, "hmc5883l_task", 1024*4, &weather_data, 5, NULL);

    ESP_LOGI(TAG, "End of initialization. server=%p", server);

    while (1) {
        blink_led();
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

}
