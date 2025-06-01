#include "car_controller.h"

int nos_flag = 0;
int rotation_flag = 0;

void nos(){
    nos_flag = !nos_flag;
}

void forward(){
    gpio_set_level(GPIO_OUTPUT_PIN_12, 1);
    gpio_set_level(GPIO_OUTPUT_PIN_13, 0);
    gpio_set_level(GPIO_OUTPUT_PIN_14, 1);
    gpio_set_level(GPIO_OUTPUT_PIN_15, 0);

    rotation_control();
}

void backward(){
    gpio_set_level(GPIO_OUTPUT_PIN_12, 0);
    gpio_set_level(GPIO_OUTPUT_PIN_13, 1);
    gpio_set_level(GPIO_OUTPUT_PIN_14, 0);
    gpio_set_level(GPIO_OUTPUT_PIN_15, 1);

    rotation_control();
}

void rotation_control(){
    int speed = (nos_flag) ? 255 : 128; // NOS speed
    if (rotation_flag == 0)
    {
        ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, speed); // 100% duty
        ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);
        ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1, speed*0.5); // 100% duty
        ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1);
    }
    else if (rotation_flag == 1)
    {
        ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, speed*0.5); // 100% duty
        ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);
        ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1, speed); // 100% duty
        ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1);
    }
    else
    {
        ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, speed); // 100% duty
        ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);
        ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1, speed); // 100% duty
        ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1);
    }
}

void turn_right(){
    rotation_flag = 0;
}

void turn_left(){
    rotation_flag = 1;
}

void no_turn(){
    rotation_flag = 2;
}

void stop(){
    gpio_set_level(GPIO_OUTPUT_PIN_12, 0);
    gpio_set_level(GPIO_OUTPUT_PIN_13, 0);
    gpio_set_level(GPIO_OUTPUT_PIN_14, 0);
    gpio_set_level(GPIO_OUTPUT_PIN_15, 0);

    ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, 0); // 100% duty
    ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);
    ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1, 0); // 100% duty
    ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1);
}