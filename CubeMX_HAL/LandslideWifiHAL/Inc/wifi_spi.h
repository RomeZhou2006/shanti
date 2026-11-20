#pragma once

#include <stddef.h>
#include <stdint.h>
#include "stm32f4xx_hal.h"

typedef enum {
    WIFI_OK = 0,
    WIFI_TIMEOUT,
    WIFI_SPI_ERROR,
    WIFI_BAD_REPLY,
    WIFI_TOO_LONG
} wifi_result_t;

wifi_result_t wifi_module_init(SPI_HandleTypeDef *spi, char *version, size_t version_size);
wifi_result_t wifi_join(const char *ssid, const char *password);
wifi_result_t wifi_udp_discovery_open(uint16_t remote_port);
wifi_result_t wifi_tcp_connect(const char *ip, uint16_t port);
wifi_result_t wifi_tcp_send(const uint8_t *data, size_t length);
wifi_result_t wifi_tcp_poll(void);
size_t wifi_tcp_read(uint8_t *out, size_t capacity);
const char *wifi_error_name(wifi_result_t result);
