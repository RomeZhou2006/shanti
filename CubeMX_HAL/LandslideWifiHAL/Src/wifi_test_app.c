#include "app_all.h"
#include "wifi_credentials.h"
#include <stdio.h>
#include <string.h>

static uint32_t sequence;
static char command_line[64];
static size_t command_used;
static wifi_result_t send_text(const char *line)
{
    return wifi_tcp_send((const uint8_t *)line, strlen(line));
}

static wifi_result_t handle_command(const char *line)
{
    if (strcmp(line, "PING") == 0) return send_text("PONG\n");
    if (strcmp(line, "LED,1") == 0) {
        HAL_GPIO_WritePin(BOARD_LED_GPIO_Port, BOARD_LED_Pin, GPIO_PIN_SET);
        return send_text("OK,LED,1\n");
    }
    if (strcmp(line, "LED,0") == 0) {
        HAL_GPIO_WritePin(BOARD_LED_GPIO_Port, BOARD_LED_Pin, GPIO_PIN_RESET);
        return send_text("OK,LED,0\n");
    }
    if (strcmp(line, "BUZZER,1") == 0) {
        HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_SET);
        return send_text("OK,BUZZER,1\n");
    }
    if (strcmp(line, "BUZZER,0") == 0) {
        HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);
        return send_text("OK,BUZZER,0\n");
    }
    return send_text("ERR,CMD\n");
}

static wifi_result_t process_received(void)
{
    uint8_t bytes[32];
    size_t count;
    while ((count = wifi_tcp_read(bytes, sizeof(bytes))) != 0) {
        for (size_t i = 0; i < count; ++i) {
            if (bytes[i] == '\r') continue;
            if (bytes[i] == '\n') {
                command_line[command_used] = '\0';
                wifi_result_t result = handle_command(command_line);
                command_used = 0;
                if (result != WIFI_OK) return result;
            } else if (command_used < sizeof(command_line) - 1) {
                command_line[command_used++] = (char)bytes[i];
            } else {
                command_used = 0;
            }
        }
    }
    return WIFI_OK;
}

static uint8_t parse_host_reply(const char *reply, char ip[16], uint16_t *port)
{
    unsigned a, b, c, d, p;
    char extra;
    int count = sscanf(reply, "HOST,%3u.%3u.%3u.%3u,%5u%c",
                       &a, &b, &c, &d, &p, &extra);
    if (count != 5 || a > 255u || b > 255u || c > 255u || d > 255u ||
        p == 0u || p > 65535u) return 0;
    int n = snprintf(ip, 16, "%u.%u.%u.%u", a, b, c, d);
    if (n <= 0 || n >= 16) return 0;
    *port = (uint16_t)p;
    return 1;
}

static wifi_result_t discover_host(char ip[16], uint16_t *port)
{
    static const char request[] = "DISCOVER,LS1\n";
    char reply[48];
    size_t used = 0;
    uint32_t start = HAL_GetTick();
    uint32_t last_send = start - 1000u;

    while ((uint32_t)(HAL_GetTick() - start) < 30000u) {
        uint32_t now = HAL_GetTick();
        if ((uint32_t)(now - last_send) >= 1000u) {
            wifi_result_t sent = wifi_tcp_send((const uint8_t *)request,
                                               sizeof(request) - 1u);
            if (sent != WIFI_OK) return sent;
            last_send = now;
        }

        wifi_result_t polled = wifi_tcp_poll();
        if (polled != WIFI_OK && polled != WIFI_TIMEOUT) return polled;
        uint8_t bytes[32];
        size_t length;
        while ((length = wifi_tcp_read(bytes, sizeof(bytes))) != 0u) {
            for (size_t i = 0; i < length; ++i) {
                if (bytes[i] == '\r') continue;
                if (bytes[i] == '\n') {
                    reply[used] = '\0';
                    if (parse_host_reply(reply, ip, port)) return WIFI_OK;
                    used = 0;
                } else if (used < sizeof(reply) - 1u) {
                    reply[used++] = (char)bytes[i];
                } else {
                    used = 0;
                }
            }
        }
        HAL_Delay(50);
    }
    return WIFI_TIMEOUT;
}

void wifi_test_app_run(SPI_HandleTypeDef *spi)
{

    char version[33];
    char line[40];
    char server_ip[16];
    uint16_t server_port = TEST_SERVER_PORT;
    uint32_t last_heartbeat = HAL_GetTick();
    unsigned consecutive_errors = 0;
    uint8_t connected = 0;

    for (;;) {
        if (!connected) {
            command_used = 0;
            wifi_result_t result = wifi_module_init(spi, version, sizeof(version));
            if (result == WIFI_OK)
                result = wifi_join(TEST_WIFI_SSID, TEST_WIFI_PASSWORD);
            if (result == WIFI_OK)
                result = wifi_udp_discovery_open(TEST_DISCOVERY_PORT);
            if (result == WIFI_OK)
                result = discover_host(server_ip, &server_port);
            /* 重新初始化模块，退出 UDP 模式后按发现的地址建立 TCP 连接。 */
            if (result == WIFI_OK)
                result = wifi_module_init(spi, version, sizeof(version));
            if (result == WIFI_OK)
                result = wifi_join(TEST_WIFI_SSID, TEST_WIFI_PASSWORD);
            if (result == WIFI_OK)
                result = wifi_tcp_connect(server_ip, server_port);
            if (result != WIFI_OK) {
                HAL_Delay(3000);
                continue;
            }
            connected = 1;
            consecutive_errors = 0;
            last_heartbeat = HAL_GetTick();
            if (send_text("HELLO,STM32F407\n") != WIFI_OK) connected = 0;
            continue;
        }

        wifi_result_t result = wifi_tcp_poll();
        /* 暂无下行数据是正常空闲状态，发送心跳时再检查链路。 */
        if (result == WIFI_TIMEOUT) result = WIFI_OK;
        if (result == WIFI_OK) result = process_received();

        if ((uint32_t)(HAL_GetTick() - last_heartbeat) >= 1000u) {
            last_heartbeat = HAL_GetTick();
            /* 四项数据均为演示值，不代表传感器实测结果。 */
            unsigned rain = (unsigned)((sequence * 7u) % 80u);
            unsigned soil = 35u + (unsigned)((sequence * 3u) % 66u);
            unsigned tilt = (unsigned)((sequence * 2u) % 31u);
            unsigned displacement = (unsigned)(sequence % 26u);
            int n = snprintf(line, sizeof(line), "DEMO,%lu,%u,%u,%u,%u\n",
                             (unsigned long)sequence++, rain, soil, tilt, displacement);
            if (n > 0 && n <= 32 && n < (int)sizeof(line)) {
                wifi_result_t send_result = wifi_tcp_send((const uint8_t *)line, (size_t)n);
                if (result == WIFI_OK) result = send_result;
            }
        }

        if (result != WIFI_OK) {
            if (++consecutive_errors >= 3u) connected = 0;
        } else {
            consecutive_errors = 0;
        }
        HAL_Delay(50);
    }
}
