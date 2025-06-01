#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"


#include "pins.h"
#include "app_httpd.h"


static const char *TAG = "wifi_main";

// IP alındığında çağrılan callback
static void on_got_ip(void *arg, esp_event_base_t event_base,
                      int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    ESP_LOGI(TAG, "Bağlandı! IP adresi: " IPSTR, IP2STR(&event->ip_info.ip));
}

void configure_gpio()
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_OUTPUT_PIN_12) |
                        (1ULL << GPIO_OUTPUT_PIN_13) |
                        (1ULL << GPIO_OUTPUT_PIN_14) |
                        (1ULL << GPIO_OUTPUT_PIN_15),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
}

void configure_pwm_pins()
{
    // 1. Timer yapılandır
    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz = 5000,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .timer_num = LEDC_TIMER_0
    };
    ledc_timer_config(&ledc_timer);

    // 2. Kanal/PIN eşlemesi (2 kanal kullanılacaksa sadece 2 eleman tanımlanmalı)
    ledc_channel_config_t channels[2] = {
        {
            .channel    = LEDC_CHANNEL_0,
            .gpio_num   = GPIO_PWM_PIN_2,  // ENA
            .speed_mode = LEDC_HIGH_SPEED_MODE,
            .hpoint     = 0,
            .timer_sel  = LEDC_TIMER_0,
            .duty       = 0
        },
        {
            .channel    = LEDC_CHANNEL_1,
            .gpio_num   = GPIO_PWM_PIN_16,  // ENB
            .speed_mode = LEDC_HIGH_SPEED_MODE,
            .hpoint     = 0,
            .timer_sel  = LEDC_TIMER_0,
            .duty       = 0
        }
    };

    for (int i = 0; i < 2; i++) {
        ledc_channel_config(&channels[i]);
    }
}

void wifi_init_sta()
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);


    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip, NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "Zyxel_2D79",
            .password = "batuhan1744",
        },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
    esp_wifi_connect();
    ESP_LOGI(TAG, "Bağlanıyor...");
    startCameraServer();
}

void app_main()
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init_sta();
    
    configure_gpio();
    configure_pwm_pins();
    
    while (1) {
       vTaskDelay(pdMS_TO_TICKS(10));
    }
}