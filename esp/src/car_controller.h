#ifndef CAR_CONTROLLER_H
#define CAR_CONTROLLER_H

#include "pins.h"
#include "driver/ledc.h"
#include "esp_err.h"




void nos();

void forward();
void backward();


void turn_right();
void turn_left();
void no_turn();
void rotation_control();
void stop();

#endif // CAR_CONTROLLER_H