// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "fb_gfx.h"
#include "esp32-hal-ledc.h"
#include "sdkconfig.h"
#include "camera_index.h"
#include "board_config.h"
#include "Arduino.h"

#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_ARDUHAL_ESP_LOG)
#include "esp32-hal-log.h"
#endif

// user defines start
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#define GPIO_PWM_PIN_2  2
#define GPIO_PWM_PIN_16 16
#define PWM_FREQ        500
#define PWM_RESOLUTION  8
// user defines end

static bool flash_state = false;

#if defined(LED_GPIO_NUM)
#define CONFIG_LED_MAX_INTENSITY 255
int led_duty = 0;
bool isStreaming = false;
#endif

typedef struct {
  httpd_req_t *req;
  size_t len;
} jpg_chunking_t;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %d.%06d\r\n\r\n";

httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

typedef struct {
  size_t size;
  size_t index;
  size_t count;
  int sum;
  int *values;
} ra_filter_t;

static ra_filter_t ra_filter;

static ra_filter_t *ra_filter_init(ra_filter_t *filter, size_t sample_size) {
  memset(filter, 0, sizeof(ra_filter_t));
  filter->values = (int *)malloc(sample_size * sizeof(int));
  if (!filter->values) return NULL;
  memset(filter->values, 0, sample_size * sizeof(int));
  filter->size = sample_size;
  return filter;
}

#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
static int ra_filter_run(ra_filter_t *filter, int value) {
  if (!filter->values) return value;
  filter->sum -= filter->values[filter->index];
  filter->values[filter->index] = value;
  filter->sum += filter->values[filter->index];
  filter->index = (filter->index + 1) % filter->size;
  if (filter->count < filter->size) filter->count++;
  return filter->sum / filter->count;
}
#endif

// --- LED Fonksiyonu ---
#if defined(LED_GPIO_NUM)
void enable_led(bool en) {
  int duty = en ? led_duty : 0;
  if (en && isStreaming && (led_duty > CONFIG_LED_MAX_INTENSITY)) {
    duty = CONFIG_LED_MAX_INTENSITY;
  }
  // v2.0.17 için Kanal 2
  ledcWrite(2, duty);
  log_i("Set LED intensity to %d", duty);
}
#endif

void setup_pwm() {
  ledcSetup(0, PWM_FREQ, PWM_RESOLUTION); 
  ledcSetup(1, PWM_FREQ, PWM_RESOLUTION); 

  ledcAttachPin(GPIO_PWM_PIN_2, 0); 
  ledcAttachPin(GPIO_PWM_PIN_16, 1); 
  
  ledcWrite(0, 0); 
  ledcWrite(1, 0);
}

void setupLedFlash() {
#if defined(LED_GPIO_NUM)
  ledcSetup(2, 5000, 8); 
  ledcAttachPin(LED_GPIO_NUM, 2);
  ledcWrite(2, 0);
#endif
}

// --- API Handler Fonksiyonları ---

static esp_err_t bmp_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  fb = esp_camera_fb_get();
  if (!fb) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  httpd_resp_set_type(req, "image/x-windows-bmp");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  uint8_t *buf = NULL;
  size_t buf_len = 0;
  bool converted = frame2bmp(fb, &buf, &buf_len);
  esp_camera_fb_return(fb);
  if (!converted) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  res = httpd_resp_send(req, (const char *)buf, buf_len);
  free(buf);
  return res;
}

static size_t jpg_encode_stream(void *arg, size_t index, const void *data, size_t len) {
  jpg_chunking_t *j = (jpg_chunking_t *)arg;
  if (!index) j->len = 0;
  if (httpd_resp_send_chunk(j->req, (const char *)data, len) != ESP_OK) return 0;
  j->len += len;
  return len;
}

static esp_err_t capture_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
#if defined(LED_GPIO_NUM)
  enable_led(true);
  vTaskDelay(150 / portTICK_PERIOD_MS);
  fb = esp_camera_fb_get();
  enable_led(false);
#else
  fb = esp_camera_fb_get();
#endif
  if (!fb) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  if (fb->format == PIXFORMAT_JPEG) {
    res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
  } else {
    jpg_chunking_t jchunk = {req, 0};
    res = frame2jpg_cb(fb, 80, jpg_encode_stream, &jchunk) ? ESP_OK : ESP_FAIL;
    httpd_resp_send_chunk(req, NULL, 0);
  }
  esp_camera_fb_return(fb);
  return res;
}

static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  struct timeval _timestamp;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t *_jpg_buf = NULL;
  char *part_buf[128];
  static int64_t last_frame = 0;
  if (!last_frame) last_frame = esp_timer_get_time();

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if (res != ESP_OK) return res;
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

#if defined(LED_GPIO_NUM)
  isStreaming = true;
  enable_led(true);
#endif

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      res = ESP_FAIL;
    } else {
      _timestamp.tv_sec = fb->timestamp.tv_sec;
      _timestamp.tv_usec = fb->timestamp.tv_usec;
      if (fb->format != PIXFORMAT_JPEG) {
        bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
        esp_camera_fb_return(fb);
        fb = NULL;
        if (!jpeg_converted) res = ESP_FAIL;
      } else {
        _jpg_buf_len = fb->len;
        _jpg_buf = fb->buf;
      }
    }
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    if (res == ESP_OK) {
      size_t hlen = snprintf((char *)part_buf, 128, _STREAM_PART, _jpg_buf_len, _timestamp.tv_sec, _timestamp.tv_usec);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    }
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    if (fb) { esp_camera_fb_return(fb); fb = NULL; _jpg_buf = NULL; } 
    else if (_jpg_buf) { free(_jpg_buf); _jpg_buf = NULL; }
    if (res != ESP_OK) break;
  }

#if defined(LED_GPIO_NUM)
  isStreaming = false;
  enable_led(false);
#endif
  return res;
}

static esp_err_t parse_get(httpd_req_t *req, char **obuf) {
  char *buf = NULL;
  size_t buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1) {
    buf = (char *)malloc(buf_len);
    if (!buf) { httpd_resp_send_500(req); return ESP_FAIL; }
    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) { *obuf = buf; return ESP_OK; }
    free(buf);
  }
  httpd_resp_send_404(req);
  return ESP_FAIL;
}

static esp_err_t cmd_handler(httpd_req_t *req) {
  char *buf = NULL;
  char variable[32];
  char value[32];
  if (parse_get(req, &buf) != ESP_OK) return ESP_FAIL;
  if (httpd_query_key_value(buf, "var", variable, sizeof(variable)) != ESP_OK || httpd_query_key_value(buf, "val", value, sizeof(value)) != ESP_OK) {
    free(buf);
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }
  free(buf);
  int val = atoi(value);
  sensor_t *s = esp_camera_sensor_get();
  int res = 0;
  if (!strcmp(variable, "framesize")) { if (s->pixformat == PIXFORMAT_JPEG) res = s->set_framesize(s, (framesize_t)val); }
  else if (!strcmp(variable, "quality")) res = s->set_quality(s, val);
  else if (!strcmp(variable, "led_intensity")) {
#if defined(LED_GPIO_NUM)
    led_duty = val;
    if (isStreaming) enable_led(true);
#endif
  }
  else res = -1;
  if (res < 0) return httpd_resp_send_500(req);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

static esp_err_t status_handler(httpd_req_t *req) {
  static char json_response[1024];
  sensor_t *s = esp_camera_sensor_get();
  char *p = json_response;
  *p++ = '{';
  p += sprintf(p, "\"framesize\":%u,", s->status.framesize);
#if defined(LED_GPIO_NUM)
  p += sprintf(p, "\"led_intensity\":%u", led_duty);
#else
  p += sprintf(p, "\"led_intensity\":%d", -1);
#endif
  *p++ = '}';
  *p++ = 0;
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, json_response, strlen(json_response));
}

// --- Motor Kontrol ---

void move(int right_speed, int left_speed){
  if(left_speed < 0){
    gpio_set_level(GPIO_NUM_12, 1);
    gpio_set_level(GPIO_NUM_13, 0);
    left_speed *= -1;
  } else {
    gpio_set_level(GPIO_NUM_12, 0);
    gpio_set_level(GPIO_NUM_13, 1);
  }
  if(right_speed < 0){
    gpio_set_level(GPIO_NUM_14, 1);
    gpio_set_level(GPIO_NUM_15, 0);
    right_speed *= -1;
  } else {
    gpio_set_level(GPIO_NUM_14, 0);
    gpio_set_level(GPIO_NUM_15, 1);
  }
  ledcWrite(0, right_speed); 
  ledcWrite(1, left_speed);
}

void stop(){
  gpio_set_level(GPIO_NUM_12, 0);
  gpio_set_level(GPIO_NUM_13, 0);
  gpio_set_level(GPIO_NUM_14, 0);
  gpio_set_level(GPIO_NUM_15, 0);
  ledcWrite(0, 0);
  ledcWrite(1, 0);
}

typedef enum {
    DIR_STOP = 0,
    DIR_FORWARD,
    DIR_BACKWARD
} motor_dir_t;

motor_dir_t parse_motor_dir(const char *dir)
{
    if (strcmp(dir, "forward") == 0)  return DIR_FORWARD;
    if (strcmp(dir, "backward") == 0) return DIR_BACKWARD;
    return DIR_STOP;
}

esp_err_t move_handler(httpd_req_t *req)
{
    char *buf = NULL;
    if (parse_get(req, &buf) != ESP_OK || buf == NULL)
        return ESP_FAIL;

    char rs_str[16], ls_str[16];
    char rd_str[16], ld_str[16];

    int right_speed = 0;
    int left_speed  = 0;

    motor_dir_t right_dir = DIR_STOP;
    motor_dir_t left_dir  = DIR_STOP;

    if (httpd_query_key_value(buf, "right", rs_str, sizeof(rs_str)) == ESP_OK)
        right_speed = atoi(rs_str);

    if (httpd_query_key_value(buf, "left", ls_str, sizeof(ls_str)) == ESP_OK)
        left_speed = atoi(ls_str);

    if (httpd_query_key_value(buf, "right_dir", rd_str, sizeof(rd_str)) == ESP_OK)
        right_dir = parse_motor_dir(rd_str);

    if (httpd_query_key_value(buf, "left_dir", ld_str, sizeof(ld_str)) == ESP_OK)
        left_dir = parse_motor_dir(ld_str);

    free(buf);

    move(
        (right_dir == DIR_BACKWARD ? -right_speed : right_speed),
        (left_dir  == DIR_BACKWARD ? -left_speed  : left_speed)
    );

    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}


esp_err_t stop_handler(httpd_req_t *req) {
  stop();
  httpd_resp_send(req, "Stopped", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

esp_err_t toggle_flash_handler(httpd_req_t *req) {
    flash_state = !flash_state;
#if defined(LED_GPIO_NUM)
    led_duty = CONFIG_LED_MAX_INTENSITY;
    enable_led(flash_state);
#endif
    httpd_resp_send(req, flash_state ? "ON" : "OFF", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  return httpd_resp_send(req, (const char *)index_ov2640_html_gz, index_ov2640_html_gz_len);
}

void setup_motor_pins() {
  gpio_config_t io_conf = {};
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_OUTPUT;
  io_conf.pin_bit_mask = (1ULL << GPIO_NUM_12) | (1ULL << GPIO_NUM_13) | (1ULL << GPIO_NUM_14) | (1ULL << GPIO_NUM_15);
  gpio_config(&io_conf);
  stop();
}

void startCameraServer() {
  setup_motor_pins();
  setup_pwm();
  setupLedFlash();
  
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 16;

  httpd_uri_t move_uri = { .uri = "/api/move", .method = HTTP_GET, .handler = move_handler };
  httpd_uri_t stop_uri = { .uri = "/api/stop", .method = HTTP_GET, .handler = stop_handler };
  httpd_uri_t flash_uri = { .uri = "/api/flash", .method = HTTP_GET, .handler = toggle_flash_handler };
  httpd_uri_t index_uri = { .uri = "/", .method = HTTP_GET, .handler = index_handler };
  httpd_uri_t status_uri = { .uri = "/status", .method = HTTP_GET, .handler = status_handler };
  httpd_uri_t cmd_uri = { .uri = "/control", .method = HTTP_GET, .handler = cmd_handler };
  httpd_uri_t capture_uri = { .uri = "/capture", .method = HTTP_GET, .handler = capture_handler };
  httpd_uri_t stream_uri = { .uri = "/stream", .method = HTTP_GET, .handler = stream_handler };

  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &index_uri);
    httpd_register_uri_handler(camera_httpd, &cmd_uri);
    httpd_register_uri_handler(camera_httpd, &status_uri);
    httpd_register_uri_handler(camera_httpd, &capture_uri);
    httpd_register_uri_handler(camera_httpd, &move_uri);
    httpd_register_uri_handler(camera_httpd, &stop_uri);
    httpd_register_uri_handler(camera_httpd, &flash_uri);
    httpd_register_uri_handler(camera_httpd, &stream_uri);
  }

  config.server_port += 1;
  config.ctrl_port += 1;
  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
  }
}