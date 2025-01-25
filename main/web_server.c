#include <string.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_event.h"
#include "esp_system.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_wifi.h"

#include "freertos/queue.h"
#include "cJSON.h"

#include "weather.h"

static const char *TAG = "web_server";

// Functions for web socket handling
httpd_handle_t server;
int client_fd;

// callback function to be put onto httpd work queue
static void ws_async_send(void *arg)
{
    ESP_LOGD(TAG, "ws_async_send with arg = %p. client_fd = %d", arg, client_fd);

    // Create JSON object
    cJSON *root = cJSON_CreateObject();
    
    // Add sensor data from weather_data structure
    cJSON *temp = cJSON_CreateObject();
    cJSON_AddNumberToObject(temp, "c", weather_data.temperature);
    cJSON_AddNumberToObject(temp, "f", (weather_data.temperature * 9.0/5.0) + 32);
    cJSON_AddItemToObject(root, "temperature", temp);

    cJSON *pressure = cJSON_CreateObject();
    cJSON_AddNumberToObject(pressure, "pa", weather_data.pressure);
    cJSON_AddNumberToObject(pressure, "inhg", weather_data.pressure / 3386.0);
    cJSON_AddItemToObject(root, "pressure", pressure);

    cJSON *altitude = cJSON_CreateObject();
    cJSON_AddNumberToObject(altitude, "m", weather_data.altitude);
    cJSON_AddNumberToObject(altitude, "ft", weather_data.altitude * 3.281);
    cJSON_AddItemToObject(root, "altitude", altitude);

    cJSON_AddNumberToObject(root, "heading", weather_data.angle);

    cJSON *magnetic = cJSON_CreateObject();
    cJSON_AddNumberToObject(magnetic, "x", weather_data.x);
    cJSON_AddNumberToObject(magnetic, "y", weather_data.y);
    cJSON_AddNumberToObject(magnetic, "z", weather_data.z);
    cJSON_AddItemToObject(root, "magnetic", magnetic);

    // Convert to string
    char *json_string = cJSON_PrintUnformatted(root);
    
    // Send via websocket
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t*)json_string;
    ws_pkt.len = strlen(json_string);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_send_frame_async(server, client_fd, &ws_pkt);
    ESP_LOGD(TAG, "httpd_ws_send_frame_async returned %d", (int)ret);

    cJSON_Delete(root);
    free(json_string);
}

static esp_err_t ws_data_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
         return ESP_OK;
    }

    client_fd = httpd_req_to_sockfd(req);
    ESP_LOGI(TAG, "ws_data_handler set client_fd %d", client_fd);

    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    /* Set max_len = 0 to get the frame len */
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }
    ESP_LOGI(TAG, "frame len is %d", ws_pkt.len);
    if (ws_pkt.len) {
        /* ws_pkt.len + 1 is for NULL termination as we are expecting a string */
        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL) {
            ESP_LOGE(TAG, "Failed to calloc memory for buf");
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;
        /* Set max_len = ws_pkt.len to get the frame payload */
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
            free(buf);
            return ret;
        }
        ESP_LOGI(TAG, "Got packet with message: %s", ws_pkt.payload);
    }
    ESP_LOGI(TAG, "Packet type: %d", ws_pkt.type);

    ret = httpd_ws_send_frame(req, &ws_pkt);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_ws_send_frame failed with %d", ret);
    }
    free(buf);
    return ret;
}


// Functions to implement web pages
/* Function to free context */
static void adder_free_func(void *ctx)
{
    ESP_LOGI(TAG, "/ Free Context function called");
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

#include "web_content.h"

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
        httpd_resp_send(req, css__weather, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    if (strcmp(req->uri, "/weather.js") == 0) {
        httpd_resp_set_type(req, "text/javascript");
        httpd_resp_send(req, js__weather, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }    

    httpd_resp_send(req, html__index, HTTPD_RESP_USE_STRLEN);
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
static const httpd_uri_t weather_get4 = {
    .uri      = "/weather.js",
    .method   = HTTP_GET,
    .handler  = weather_get_handler,
    .user_ctx = &visitors
};
static const httpd_uri_t websocket = {
        .uri        = "/ws",
        .method     = HTTP_GET,
        .handler    = ws_data_handler,
        .user_ctx   = NULL,
        .is_websocket = true
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

httpd_handle_t start_webserver(void);
static esp_err_t stop_webserver(httpd_handle_t server);

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

httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    
    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);

    if (httpd_start(&server, &config) == ESP_OK) {
        /* Register event handlers to stop the server when Wi-Fi or Ethernet is disconnected,
        * and re-start it upon connection.
        */
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &server));
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &server));
    
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &websocket);
        httpd_register_uri_handler(server, &weather_get);
        httpd_register_uri_handler(server, &weather_get2);
        httpd_register_uri_handler(server, &weather_get3);
        httpd_register_uri_handler(server, &weather_get4);                
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
    esp_err_t ret = httpd_stop(server);
    server = NULL;
    return ret;
}

// Add function to send sensor data
esp_err_t send_sensor_data(sensor_message_t *msg)
{
    if (server && client_fd) {
        return httpd_queue_work(server, ws_async_send, NULL);
    }
    return ESP_FAIL;
}