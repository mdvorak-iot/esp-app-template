cmake_minimum_required(VERSION 3.13.0)

idf_component_register(
        SRCS
        app_main.c
        app_status.c
        INCLUDE_DIRS .
        REQUIRES
        freertos
        nvs_flash
        button
        status_led
        wifi_auto_prov
        wifi_reconnect
)
