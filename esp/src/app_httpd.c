#include "app_httpd.h"


esp_err_t startCameraServer(void)
{
    httpd_handle_t camera_httpd = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    config.server_port = SERVER_PORT;
    config.ctrl_port = SERVER_CTRL_PORT;

    // Start the httpd server
    if (httpd_start(&camera_httpd, &config) == ESP_OK) {
        // Set URI handlers
        httpd_uri_t drive_uri = {
            .uri       = "/drive",
            .method    = HTTP_GET,
            .handler   = drive_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(camera_httpd, &drive_uri);
                // /stream URI handler
        httpd_uri_t stream_uri = {
            .uri       = "/stream",
            .method    = HTTP_GET,
            .handler   = stream_handler,  // Aşağıda örnek stream_handler var
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(camera_httpd, &stream_uri);
    }
    
    return ESP_OK;
}

esp_err_t drive_handler(httpd_req_t *req)
{
    char buf[64];
    size_t buf_len = httpd_req_get_url_query_len(req) + 1;

    
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
        char dir[16];
        if (httpd_query_key_value(buf, "dir", dir, sizeof(dir)) == ESP_OK) {
            if (strcmp(dir, "forward") == 0) {forward();} 
            else if (strcmp(dir, "backward") == 0) {backward();}
            else if (strcmp(dir, "left") == 0) {turn_left();}
            else if (strcmp(dir, "right") == 0) {turn_right();}
            else if (strcmp(dir, "nitro") == 0) {nos();}
            else if (strcmp(dir, "stop") == 0) {
                gpio_set_level(GPIO_OUTPUT_PIN_12, 0);
                gpio_set_level(GPIO_OUTPUT_PIN_13, 0);
                gpio_set_level(GPIO_OUTPUT_PIN_14, 0);
                gpio_set_level(GPIO_OUTPUT_PIN_15, 0);
            } else {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid direction");
                return ESP_FAIL;
            }
        }
    }

    httpd_resp_send(req, "Drive OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t stream_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len;
    uint8_t * _jpg_buf;
    char * part_buf[64];

    res = httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=frame");

    while (true) {
        fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Camera capture failed");
            res = ESP_FAIL;
            break;
        }

        if (fb->format != PIXFORMAT_JPEG) {
            bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
            esp_camera_fb_return(fb);
            if (!jpeg_converted) {
                ESP_LOGE(TAG, "JPEG compression failed");
                res = ESP_FAIL;
                break;
            }
        } else {
            _jpg_buf_len = fb->len;
            _jpg_buf = fb->buf;
        }

        size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
        res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        res |= httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
        res |= httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));

        if (fb->format != PIXFORMAT_JPEG) {
            free(_jpg_buf);
        }
        esp_camera_fb_return(fb);

        if (res != ESP_OK) break;
    }

    return res;
}
