#include "hardware/gpio.h"
#include "pico/binary_info.h"
#include "pico/stdlib.h"
#include "ws2812.pio.h"
#include <stdio.h>
#include <string.h>

#define LED_PIN 29
#define WS2812_PIN 16

// these two functions from https://github.com/raspberrypi/pico-examples/blob/master/pio/ws2812/ws2812.c
static inline void put_pixel(PIO pio, uint sm, uint32_t pixel_grb) { pio_sm_put_blocking(pio, sm, pixel_grb << 8u); }

static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b) { return ((uint32_t)(r) << 8) | ((uint32_t)(g) << 16) | (uint32_t)(b); }

int main() {

    bi_decl(bi_program_description("This is a test binary."));
    bi_decl(bi_1pin_with_name(LED_PIN, "External LED"));
    bi_decl(bi_1pin_with_name(WS2812_PIN, "On-board LED"));

    stdio_init_all();

    PIO pio;
    uint sm;
    uint offset;

    bool success = pio_claim_free_sm_and_add_program_for_gpio_range(&ws2812_program, &pio, &sm, &offset, WS2812_PIN, 1, true);
    hard_assert(success);

    ws2812_program_init(pio, sm, offset, WS2812_PIN, 800000, false);

    puts("ping!");

    uint8_t buf[3];
    memset(buf, 0, 3);

    scanf("%*[^\n]\n");

    while (1) {
        fread(buf, 1, 3, stdin);

        put_pixel(pio, sm, urgb_u32(buf[1], buf[0], buf[2]));
    }
}
