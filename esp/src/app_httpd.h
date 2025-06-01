#ifndef APP_HTTPD_H
#define APP_HTTPD_H

#include "esp_err.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include <string.h>
#include "car_controller.h"
#include <esp_camera.h>

#define SERVER_PORT 80
#define SERVER_CTRL_PORT 81

esp_err_t startCameraServer(void);
esp_err_t drive_handler(httpd_req_t *req);
esp_err_t stream_handler(httpd_req_t *req);

#endif // APP_HTTPD_H
