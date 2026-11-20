/* 逐飞 Wi-Fi SPI 命令帧的精简 STM32 HAL 移植实现。
 * 协议参考逐飞 WIFI_SPI_Product 资料和用户原有的 TC264D 驱动。
 * 仅用于 UDP 地址发现和 TCP 连通性测试，不是完整的通用驱动。
 */
#include "wifi_spi.h"
#include <stdio.h>
#include <string.h>

#define WIFI_CS_PORT GPIOE
#define WIFI_CS_PIN GPIO_PIN_1
#define WIFI_INT_PORT GPIOE
#define WIFI_INT_PIN GPIO_PIN_9
#define WIFI_RST_PORT GPIOE
#define WIFI_RST_PIN GPIO_PIN_14

#define CMD_DATA 0x02u
#define CMD_SET_WIFI 0x10u
#define CMD_SET_SOCKET 0x11u
#define CMD_GET_VERSION 0x20u
#define REPLY_OK 0x80u
#define REPLY_ERROR 0x81u
#define REPLY_DATA_MORE 0x90u
#define REPLY_DATA_END 0x91u
#define REPLY_VERSION 0xA0u

static SPI_HandleTypeDef *bus;
static uint8_t receive_fifo[512];
static size_t receive_head;
static size_t receive_tail;

static void header(uint8_t out[4], uint8_t command, uint16_t length)
{
    out[0] = command;
    out[1] = 0;
    out[2] = (uint8_t)(length & 0xffu);
    out[3] = (uint8_t)(length >> 8);
}

static wifi_result_t ready(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    while (HAL_GPIO_ReadPin(WIFI_INT_PORT, WIFI_INT_PIN) != GPIO_PIN_SET) {
        if ((uint32_t)(HAL_GetTick() - start) >= timeout_ms) return WIFI_TIMEOUT;
    }
    return WIFI_OK;
}

static wifi_result_t exchange(const uint8_t *tx, uint8_t *rx, uint16_t length)
{
    uint8_t dummy_tx[32] = {0};
    uint8_t dummy_rx[32];
    while (length) {
        uint16_t part = length > sizeof(dummy_tx) ? sizeof(dummy_tx) : length;
        const uint8_t *source = tx ? tx : dummy_tx;
        uint8_t *target = rx ? rx : dummy_rx;
        if (HAL_SPI_TransmitReceive(bus, (uint8_t *)source, target, part, 1000) != HAL_OK)
            return WIFI_SPI_ERROR;
        if (tx) tx += part;
        if (rx) rx += part;
        length -= part;
    }
    return WIFI_OK;
}

static wifi_result_t write_frame(uint8_t command, const uint8_t *payload,
                                 uint16_t length)
{
    uint8_t h[4], unused[4];
    header(h, command, length);
    HAL_GPIO_WritePin(WIFI_CS_PORT, WIFI_CS_PIN, GPIO_PIN_RESET);
    wifi_result_t result = exchange(h, unused, 4);
    if (result == WIFI_OK && length) result = exchange(payload, NULL, length);
    HAL_GPIO_WritePin(WIFI_CS_PORT, WIFI_CS_PIN, GPIO_PIN_SET);
    return result;
}

static wifi_result_t read_frame(uint8_t *command, uint16_t *length,
                                uint8_t payload[32], uint16_t clock_bytes)
{
    uint8_t h[4], received[4], zeros[32] = {0};
    header(h, CMD_DATA, 0);
    HAL_GPIO_WritePin(WIFI_CS_PORT, WIFI_CS_PIN, GPIO_PIN_RESET);
    wifi_result_t result = exchange(h, received, 4);
    if (result == WIFI_OK && clock_bytes)
        result = exchange(zeros, payload, clock_bytes);
    HAL_GPIO_WritePin(WIFI_CS_PORT, WIFI_CS_PIN, GPIO_PIN_SET);
    if (result != WIFI_OK) return result;
    *command = received[0];
    *length = (uint16_t)(received[2] | ((uint16_t)received[3] << 8));
    if (clock_bytes && *length > clock_bytes) return WIFI_BAD_REPLY;
    return WIFI_OK;
}

static wifi_result_t set_parameter(uint8_t command, const uint8_t *payload,
                                   uint16_t length, uint32_t timeout_ms)
{
    uint8_t response[32], reply;
    uint16_t reply_length;
    wifi_result_t result = ready(timeout_ms);
    if (result != WIFI_OK) return result;
    result = write_frame(command, payload, length);
    if (result != WIFI_OK) return result;
    result = ready(timeout_ms);
    if (result != WIFI_OK) return result;
    result = read_frame(&reply, &reply_length, response, 0);
    if (result != WIFI_OK) return result;
    return reply == REPLY_OK ? WIFI_OK : WIFI_BAD_REPLY;
}

wifi_result_t wifi_module_init(SPI_HandleTypeDef *spi, char *version, size_t version_size)
{
    uint8_t request[32] = {CMD_GET_VERSION};
    uint8_t response[32], reply;
    uint16_t reply_length;
    if (!spi || !version || version_size == 0) return WIFI_BAD_REPLY;
    bus = spi;
    receive_head = receive_tail = 0;
    HAL_GPIO_WritePin(WIFI_CS_PORT, WIFI_CS_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(WIFI_RST_PORT, WIFI_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(10);
    HAL_GPIO_WritePin(WIFI_RST_PORT, WIFI_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(100);
    wifi_result_t result = ready(1000);
    if (result != WIFI_OK) return result;
    HAL_GPIO_WritePin(WIFI_CS_PORT, WIFI_CS_PIN, GPIO_PIN_RESET);
    result = exchange(request, NULL, sizeof(request));
    HAL_GPIO_WritePin(WIFI_CS_PORT, WIFI_CS_PIN, GPIO_PIN_SET);
    if (result != WIFI_OK) return result;
    result = ready(1000);
    if (result != WIFI_OK) return result;
    result = read_frame(&reply, &reply_length, response, 32);
    if (result != WIFI_OK) return result;
    if (reply != REPLY_VERSION) return WIFI_BAD_REPLY;
    size_t copy = reply_length < version_size - 1 ? reply_length : version_size - 1;
    memcpy(version, response, copy);
    version[copy] = '\0';
    return WIFI_OK;
}

wifi_result_t wifi_join(const char *ssid, const char *password)
{
    char payload[96];
    int n = snprintf(payload, sizeof(payload), "%s\r\n%s\r\n", ssid, password);
    if (n < 0 || n >= (int)sizeof(payload)) return WIFI_TOO_LONG;
    return set_parameter(CMD_SET_WIFI, (const uint8_t *)payload, (uint16_t)n, 10000);
}

wifi_result_t wifi_tcp_connect(const char *ip, uint16_t port)
{
    char payload[64];
    int n = snprintf(payload, sizeof(payload), "TCP\r\n%s\r\n%u\r\n6666\r\n",
                     ip, (unsigned)port);
    if (n < 0 || n >= (int)sizeof(payload)) return WIFI_TOO_LONG;
    return set_parameter(CMD_SET_SOCKET, (const uint8_t *)payload,
                         (uint16_t)n, 50000);
}

wifi_result_t wifi_udp_discovery_open(uint16_t remote_port)
{
    char payload[64];
    int n = snprintf(payload, sizeof(payload), "UDP\r\n255.255.255.255\r\n%u\r\n6666\r\n",
                     (unsigned)remote_port);
    if (n < 0 || n >= (int)sizeof(payload)) return WIFI_TOO_LONG;
    return set_parameter(CMD_SET_SOCKET, (const uint8_t *)payload,
                         (uint16_t)n, 10000);
}

static void append_reply(uint8_t command, uint16_t length, const uint8_t data[32])
{
    if (command != REPLY_DATA_MORE && command != REPLY_DATA_END) return;
    for (uint16_t i = 0; i < length; ++i) {
        size_t next = (receive_head + 1) % sizeof(receive_fifo);
        if (next == receive_tail) break;
        receive_fifo[receive_head] = data[i];
        receive_head = next;
    }
}

wifi_result_t wifi_tcp_poll(void)
{
    uint8_t command, data[32];
    uint16_t length;
    wifi_result_t result = ready(100);
    if (result != WIFI_OK) return result;
    for (unsigned i = 0; i < 16; ++i) {
        result = read_frame(&command, &length, data, 32);
        if (result != WIFI_OK) return result;
        if (command == REPLY_ERROR) return WIFI_BAD_REPLY;
        append_reply(command, length, data);
        if (command != REPLY_DATA_MORE) break;
        result = ready(100);
        if (result != WIFI_OK) return result;
    }
    return WIFI_OK;
}

wifi_result_t wifi_tcp_send(const uint8_t *data, size_t length)
{
    uint8_t out[32] = {0}, in[32], sent[4], received[4];
    uint8_t command;
    uint16_t reply_length;
    if (!data || length > sizeof(out)) return WIFI_TOO_LONG;
    memcpy(out, data, length);
    header(sent, CMD_DATA, (uint16_t)length);
    wifi_result_t result = ready(1000);
    if (result != WIFI_OK) return result;
    HAL_GPIO_WritePin(WIFI_CS_PORT, WIFI_CS_PIN, GPIO_PIN_RESET);
    result = exchange(sent, received, 4);
    if (result == WIFI_OK) result = exchange(out, in, 32);
    HAL_GPIO_WritePin(WIFI_CS_PORT, WIFI_CS_PIN, GPIO_PIN_SET);
    if (result != WIFI_OK) return result;
    command = received[0];
    reply_length = (uint16_t)(received[2] | ((uint16_t)received[3] << 8));
    if (reply_length > 32) return WIFI_BAD_REPLY;
    if (command == REPLY_ERROR) return WIFI_BAD_REPLY;
    append_reply(command, reply_length, in);
    if (command == REPLY_DATA_MORE) return wifi_tcp_poll();
    return WIFI_OK;
}

size_t wifi_tcp_read(uint8_t *out, size_t capacity)
{
    size_t count = 0;
    while (count < capacity && receive_tail != receive_head) {
        out[count++] = receive_fifo[receive_tail];
        receive_tail = (receive_tail + 1) % sizeof(receive_fifo);
    }
    return count;
}

const char *wifi_error_name(wifi_result_t result)
{
    switch (result) {
    case WIFI_OK: return "OK";
    case WIFI_TIMEOUT: return "INT timeout";
    case WIFI_SPI_ERROR: return "SPI error";
    case WIFI_BAD_REPLY: return "module reply error";
    case WIFI_TOO_LONG: return "message too long";
    default: return "unknown";
    }
}
