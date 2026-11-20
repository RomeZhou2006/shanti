#include "oled_ui.h"
#include <stdio.h>
#include <string.h>

#define OLED_ADDR       (0x3Cu << 1)
#define OLED_WIDTH      128u
#define OLED_HEIGHT     64u
#define OLED_BUFFER_LEN (OLED_WIDTH * OLED_HEIGHT / 8u)
#define KEY_LONG_MS     800u
#define KEY_DEBOUNCE_MS 30u

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    uint8_t raw_pressed;
    uint8_t stable_pressed;
    uint32_t raw_changed_at;
    uint32_t pressed_at;
} key_state_t;

static I2C_HandleTypeDef *oled_i2c;
static uint8_t oled_ready;
static uint8_t frame[OLED_BUFFER_LEN];
static char wifi_state[16] = "START";
static char key_event[20] = "NONE";
static uint8_t buzzer_on;
static uint16_t short_count;
static uint16_t long_count;
static key_state_t keys[4] = {
    {GPIOE, GPIO_PIN_7, 0, 0, 0, 0},
    {GPIOE, GPIO_PIN_8, 0, 0, 0, 0},
    {GPIOD, GPIO_PIN_10, 0, 0, 0, 0},
    {GPIOD, GPIO_PIN_11, 0, 0, 0, 0}
};

static const uint8_t *glyph(char c)
{
    static const uint8_t space[5] = {0,0,0,0,0};
    static const uint8_t digits[10][5] = {
        {0x3e,0x51,0x49,0x45,0x3e}, {0x00,0x42,0x7f,0x40,0x00},
        {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4b,0x31},
        {0x18,0x14,0x12,0x7f,0x10}, {0x27,0x45,0x45,0x45,0x39},
        {0x3c,0x4a,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
        {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1e}
    };
    static const uint8_t letters[26][5] = {
        {0x7e,0x11,0x11,0x11,0x7e}, {0x7f,0x49,0x49,0x49,0x36},
        {0x3e,0x41,0x41,0x41,0x22}, {0x7f,0x41,0x41,0x22,0x1c},
        {0x7f,0x49,0x49,0x49,0x41}, {0x7f,0x09,0x09,0x09,0x01},
        {0x3e,0x41,0x49,0x49,0x7a}, {0x7f,0x08,0x08,0x08,0x7f},
        {0x00,0x41,0x7f,0x41,0x00}, {0x20,0x40,0x41,0x3f,0x01},
        {0x7f,0x08,0x14,0x22,0x41}, {0x7f,0x40,0x40,0x40,0x40},
        {0x7f,0x02,0x0c,0x02,0x7f}, {0x7f,0x04,0x08,0x10,0x7f},
        {0x3e,0x41,0x41,0x41,0x3e}, {0x7f,0x09,0x09,0x09,0x06},
        {0x3e,0x41,0x51,0x21,0x5e}, {0x7f,0x09,0x19,0x29,0x46},
        {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7f,0x01,0x01},
        {0x3f,0x40,0x40,0x40,0x3f}, {0x1f,0x20,0x40,0x20,0x1f},
        {0x3f,0x40,0x38,0x40,0x3f}, {0x63,0x14,0x08,0x14,0x63},
        {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43}
    };
    static const uint8_t colon[5] = {0,0x36,0x36,0,0};
    static const uint8_t dash[5] = {0x08,0x08,0x08,0x08,0x08};
    static const uint8_t equal[5] = {0x14,0x14,0x14,0x14,0x14};
    static const uint8_t slash[5] = {0x20,0x10,0x08,0x04,0x02};
    if (c >= '0' && c <= '9') return digits[(unsigned)(c - '0')];
    if (c >= 'A' && c <= 'Z') return letters[(unsigned)(c - 'A')];
    if (c >= 'a' && c <= 'z') return letters[(unsigned)(c - 'a')];
    switch (c) {
    case ':': return colon;
    case '-': return dash;
    case '=': return equal;
    case '/': return slash;
    default: return space;
    }
}

static HAL_StatusTypeDef oled_command(const uint8_t *data, uint16_t length)
{
    uint8_t packet[32];
    if (length > sizeof(packet) - 1u) return HAL_ERROR;
    packet[0] = 0x00;
    memcpy(&packet[1], data, length);
    return HAL_I2C_Master_Transmit(oled_i2c, OLED_ADDR, packet,
                                   (uint16_t)(length + 1u), 100u);
}

static void oled_draw_text(uint8_t x, uint8_t y, const char *text)
{
    while (*text && x <= OLED_WIDTH - 6u) {
        const uint8_t *columns = glyph(*text++);
        for (uint8_t col = 0; col < 5u; ++col) {
            for (uint8_t row = 0; row < 7u; ++row) {
                if (columns[col] & (1u << row)) {
                    uint16_t index = (uint16_t)(x + col) +
                                     (uint16_t)((y + row) / 8u) * OLED_WIDTH;
                    frame[index] |= (uint8_t)(1u << ((y + row) & 7u));
                }
            }
        }
        x = (uint8_t)(x + 6u);
    }
}

static void oled_refresh(void)
{
    uint8_t setup[] = {0x21, 0, 127, 0x22, 0, 7};
    uint8_t packet[17];
    if (!oled_ready || oled_command(setup, sizeof(setup)) != HAL_OK) return;
    packet[0] = 0x40;
    for (uint16_t offset = 0; offset < OLED_BUFFER_LEN; offset += 16u) {
        memcpy(&packet[1], &frame[offset], 16u);
        if (HAL_I2C_Master_Transmit(oled_i2c, OLED_ADDR, packet,
                                    sizeof(packet), 100u) != HAL_OK) {
            oled_ready = 0;
            return;
        }
    }
}

static void oled_render(void)
{
    char counts[22];
    memset(frame, 0, sizeof(frame));
    oled_draw_text(0, 0, "LANDSLIDE MONITOR");
    oled_draw_text(0, 16, "WIFI:");
    oled_draw_text(36, 16, wifi_state);
    oled_draw_text(0, 32, "KEY:");
    oled_draw_text(30, 32, key_event);
    (void)snprintf(counts, sizeof(counts), "S=%03u L=%03u BZ=%s",
                   (unsigned)short_count, (unsigned)long_count,
                   buzzer_on ? "ON" : "OFF");
    oled_draw_text(0, 48, counts);
    oled_refresh();
}

void OLED_UI_Init(I2C_HandleTypeDef *i2c)
{
    static const uint8_t init_sequence[] = {
        0xae,0x20,0x00,0x40,0xa1,0xc8,0x81,0xcf,0xa6,
        0xa8,0x3f,0xd3,0x00,0xd5,0x80,0xd9,0xf1,0xda,0x12,
        0xdb,0x40,0x8d,0x14,0x21,0x00,0x7f,0x22,0x00,0x07,0xaf
    };
    oled_i2c = i2c;
    if (oled_i2c && HAL_I2C_IsDeviceReady(oled_i2c, OLED_ADDR, 2u, 100u) == HAL_OK &&
        oled_command(init_sequence, sizeof(init_sequence)) == HAL_OK) {
        oled_ready = 1;
        oled_render();
    }
}

void OLED_UI_SetWifiState(const char *state)
{
    if (!state) return;
    (void)snprintf(wifi_state, sizeof(wifi_state), "%s", state);
    oled_render();
}

void OLED_UI_SetBuzzer(uint8_t on)
{
    buzzer_on = on ? 1u : 0u;
    oled_render();
}

void OLED_UI_ProcessButtons(void)
{
    static const char *key_names[4] = {"KEY1", "KEY2", "KEY3", "KEY4"};
    uint32_t now = HAL_GetTick();
    for (uint8_t i = 0; i < 4u; ++i) {
        key_state_t *key = &keys[i];
        uint8_t pressed = HAL_GPIO_ReadPin(key->port, key->pin) == GPIO_PIN_RESET;
        if (pressed != key->raw_pressed) {
            key->raw_pressed = pressed;
            key->raw_changed_at = now;
        }
        if (pressed == key->stable_pressed ||
            (uint32_t)(now - key->raw_changed_at) < KEY_DEBOUNCE_MS) continue;

        key->stable_pressed = pressed;
        if (pressed) {
            key->pressed_at = now;
        } else {
            uint8_t is_long = (uint32_t)(now - key->pressed_at) >= KEY_LONG_MS;
            if (is_long) {
                if (long_count < 999u) ++long_count;
                (void)snprintf(key_event, sizeof(key_event), "%s LONG", key_names[i]);
            } else {
                if (short_count < 999u) ++short_count;
                (void)snprintf(key_event, sizeof(key_event), "%s SHORT", key_names[i]);
            }
            oled_render();
        }
    }
}
