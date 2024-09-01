#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_system.h"
#include "string.h"
#include "esp_http_server.h"
#include "esp_http_client.h"
#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_err.h"


#define EXAMPLE_ESP_WIFI_SSID      "TP-LINK_7E5C"
#define EXAMPLE_ESP_WIFI_PASS      "515nb666"
#define EXAMPLE_ESP_MAXIMUM_RETRY  5

static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "wifi station";
static int s_retry_num = 0;

#define LOCAL_SERVER_URL "http://192.168.27.5/upload"  // Replace with your local server URL (IP address of ESP32)

void init_camera() {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = 5;         // Y2
    config.pin_d1 = 18;        // Y3
    config.pin_d2 = 19;        // Y4
    config.pin_d3 = 21;        // Y5
    config.pin_d4 = 36;        // Y6
    config.pin_d5 = 39;        // Y7
    config.pin_d6 = 34;        // Y8
    config.pin_d7 = 35;        // Y9
    config.pin_xclk = 0;       // XCLK
    config.pin_pclk = 22;      // PCLK
    config.pin_vsync = 25;     // VSYNC
    config.pin_href = 23;      // HREF
    config.pin_sccb_sda = 26;  // SIOD
    config.pin_sccb_scl = 27;  // SIOC
    config.pin_pwdn = 32;      // PWDN
    config.pin_reset = -1;     // RESET (-1表示未使用)
    config.xclk_freq_hz = 20000000; // XCLK 频率
    config.pixel_format = PIXFORMAT_JPEG; // 输出格式
    config.frame_size = FRAMESIZE_QVGA;  // 图片尺寸
    config.jpeg_quality = 10; // 图片质量
    config.fb_count = 2; // 帧缓冲区数量

    // 初始化摄像头
    if (esp_camera_init(&config) != ESP_OK) {
        ESP_LOGE("CAMERA", "Camera init failed");
        return;
    }
}

// Upload image to the local server
esp_err_t upload_image_to_local_server(uint8_t* image_data, size_t image_size) {
    esp_err_t err;
    esp_http_client_config_t config = {
        .url = LOCAL_SERVER_URL,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "image/jpeg");
    esp_http_client_set_post_field(client, (const char*)image_data, image_size);

    err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %d",
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);

    return err;
}

// Handler to capture image and upload to local server
esp_err_t capture_and_upload_handler(httpd_req_t *req) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Camera capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Upload image to local server
    esp_err_t res = upload_image_to_local_server(fb->buf, fb->len);
    esp_camera_fb_return(fb);

    if (res == ESP_OK) {
        const char *resp_str = "Image uploaded successfully";
        httpd_resp_send(req, resp_str, strlen(resp_str));
    } else {
        httpd_resp_send_500(req);
    }

    return res;
}

esp_err_t hello_get_handler(httpd_req_t *req) {
    const char* resp_str = (const char*) "Hello, World!";
    httpd_resp_send(req, resp_str, strlen(resp_str));
    return ESP_OK;
}

esp_err_t data_upload_handler(httpd_req_t *req) {
    const int max_len = 1024;
    char buf[max_len];
    int received = httpd_req_recv(req, buf, max_len);

    if (received <= 0) {
        if (received == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }

    buf[received] = '\0'; // Ensure null-termination

    ESP_LOGI(TAG, "Received data: %s", buf);  // Process received data

    // Send response to client
    const char *resp_str = "Data received";
    httpd_resp_send(req, resp_str, strlen(resp_str));

    return ESP_OK;
}

esp_err_t capture_handler(httpd_req_t *req) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Camera capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Set the response content type to image/jpeg
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");

    // Send the image data
    esp_err_t res = httpd_resp_send(req, (const char *)fb->buf, fb->len);

    // Return the frame buffer to the driver
    esp_camera_fb_return(fb);

    return res;
}


httpd_uri_t data_upload = {
    .uri       = "/upload",
    .method    = HTTP_POST,
    .handler   = data_upload_handler,
    .user_ctx  = NULL
};

httpd_uri_t hello = {
    .uri       = "/hello",
    .method    = HTTP_GET,
    .handler   = hello_get_handler,
    .user_ctx  = NULL
};

httpd_uri_t capture_and_upload = {
    .uri       = "/capture_and_upload",
    .method    = HTTP_GET,
    .handler   = capture_and_upload_handler,
    .user_ctx  = NULL
};

httpd_uri_t capture = {
    .uri       = "/capture",
    .method    = HTTP_GET,
    .handler   = capture_handler,
    .user_ctx  = NULL
};
static httpd_handle_t start_webserver(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    // Start the httpd server
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        httpd_register_uri_handler(server, &hello);
        httpd_register_uri_handler(server, &capture_and_upload);
        httpd_register_uri_handler(server, &data_upload);
        httpd_register_uri_handler(server, &capture);
    }
    return server;
}

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        
        // Start the web server
        start_webserver();
    }
}

void wifi_init_sta(void) {
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of retries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

void app_main(void) {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();

    // Initialize camera
    init_camera();
}
