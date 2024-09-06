
#include <stdio.h>
#include <string.h>
#include <esp_log.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include <esp_wifi.h>
#include <esp_http_server.h>
#include <esp_netif.h>
#include <esp_camera.h>  // 包含摄像头的头文件

// 摄像头配置 esp32s3摄像头以后都这样配就行
static camera_config_t camera_config = {
    .pin_pwdn = -1,
    .pin_reset = -1,
    .pin_xclk = 15,
    .pin_sscb_sda = 4,
    .pin_sscb_scl = 5,
    .pin_d7 = 16,
    .pin_d6 = 17,
    .pin_d5 = 18,
    .pin_d4 = 12,
    .pin_d3 = 10,
    .pin_d2 = 8,
    .pin_d1 = 9,
    .pin_d0 = 11,
    .pin_vsync = 6,
    .pin_href = 7,
    .pin_pclk = 13,
    .xclk_freq_hz = 10000000,  // XCLK 20MHz
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,
    .pixel_format = PIXFORMAT_JPEG, // 使用 JPEG 格式
    .frame_size = FRAMESIZE_QVGA,   // 设置分辨率 UXGA
    .jpeg_quality = 10,  // 设置 JPEG 质量
    .fb_count = 2        // 帧缓冲区数量
};

// 摄像头初始化函数
static esp_err_t init_camera() {
    // 初始化摄像头
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE("Camera", "Camera Init Failed");
        return err;
    }
    return ESP_OK;
}

// 实现 upload_handler 函数
esp_err_t upload_handler(httpd_req_t *req) {
    char buffer[100];
    int ret;

    // 从请求读取数据
    ret = httpd_req_recv(req, buffer, sizeof(buffer) - 1);
    if (ret <= 0) {
        // 检查是否超时
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);  // 超时返回408
        }
        return ESP_FAIL;
    }
    
    buffer[ret] = '\0';  // 添加字符串终止符

    // 响应客户端
    httpd_resp_send(req, "Image uploaded successfully", HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

// MJPEG 流处理函数
esp_err_t stream_handler(httpd_req_t *req) {
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    char *part_buf[64];

    // 设置响应头
    httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame");

    while (true) {
        // 获取摄像头帧
        fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE("Camera", "Camera capture failed");
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }

        // 发送图片帧
        size_t hlen = snprintf((char *)part_buf, 64, "\r\n--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", fb->len);
        res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, "\r\n", 2);
        }

        // 释放摄像头帧缓冲区
        esp_camera_fb_return(fb);

        if (res != ESP_OK) {
            break;
        }
    }
    return res;
}



void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);  // 提前声明
void send_image_over_wifi(const uint8_t *image_data, size_t image_len);  // 提前声明

// 启动 Web 服务器函数
httpd_handle_t start_webserver(void) {
    httpd_handle_t server = NULL;  // 声明 server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    // 注册上传处理程序
    httpd_uri_t uri_upload = {
        .uri       = "/upload",
        .method    = HTTP_POST,
        .handler   = upload_handler,  // 使用已声明的处理程序
        .user_ctx  = NULL
    };
     httpd_uri_t uri_stream = {
        .uri       = "/stream",
        .method    = HTTP_GET,
        .handler   = stream_handler,
        .user_ctx  = NULL
    };

    // 启动 HTTP 服务器
      if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &uri_stream);
    }
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &uri_upload);
    }

    return server;
}

// WiFi 事件处理函数
void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    }
}

// WiFi 初始化函数
void wifi_init_sta(void) {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "iQOONeo9",
            .password = "12345678",
        },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
    esp_wifi_start();
}

// send_image_over_wifi 函数
void send_image_over_wifi(const uint8_t *image_data, size_t image_len) {
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &ip_info);

    char url[128];
    snprintf(url, sizeof(url), "http://%s:80/upload", ip4addr_ntoa((const ip4_addr_t *)&ip_info.ip));  // 类型转换

    // 添加上传图片的逻辑
}

// app_main 函数
void app_main(void) {
    // 初始化 NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    wifi_init_sta();

    // 初始化摄像头
    if (init_camera() != ESP_OK) {
        ESP_LOGE("Camera", "Failed to initialize camera");
        return;
    }

    // 获取摄像头帧缓冲
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) {
        send_image_over_wifi(fb->buf, fb->len);
        esp_camera_fb_return(fb);  // 释放帧缓冲
    } else {
        ESP_LOGE("Camera", "Failed to get camera frame buffer");
    }

    // 启动 Web 服务器
    start_webserver();
}

