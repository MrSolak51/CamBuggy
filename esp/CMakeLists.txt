cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_component_register(SRCS "driver/esp_camera.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp32)
project(R6_Drone)