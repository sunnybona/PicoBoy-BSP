/*
 * pb_input.cpp — 8 键直连 GPIO 驱动（高电平有效，内部下拉）
 */
#include "picoboy/pb_input.h"
#include "picoboy/board.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"

typedef struct {
    int pin;
    uint8_t bit;
} btn_t;

static const btn_t s_btns[] = {
    { PB_PIN_BTN_A,     PB_BTN_A },
    { PB_PIN_BTN_B,     PB_BTN_B },
    { PB_PIN_BTN_Y,     PB_BTN_Y },
    { PB_PIN_BTN_X,     PB_BTN_X },
    { PB_PIN_BTN_RIGHT, PB_BTN_RIGHT },
    { PB_PIN_BTN_LEFT,  PB_BTN_LEFT },
    { PB_PIN_BTN_UP,    PB_BTN_UP },
    { PB_PIN_BTN_DOWN,  PB_BTN_DOWN },
};

#define BTN_COUNT (sizeof(s_btns) / sizeof(s_btns[0]))

static uint8_t s_cur;
static uint8_t s_prev;

void pb_input_init(void)
{
    for(size_t i = 0; i < BTN_COUNT; i++)
    {
        int p = s_btns[i].pin;
        if(p < 0) continue;
        gpio_init(p);
        gpio_set_dir(p, GPIO_IN);
        gpio_pull_down(p);
    }
    s_cur = s_prev = 0;
}

uint8_t pb_input_read(void)
{
    uint8_t out = 0;
    for(size_t i = 0; i < BTN_COUNT; i++)
    {
        int p = s_btns[i].pin;
        if(p < 0) continue;
        if(gpio_get(p))
            out |= s_btns[i].bit;
    }
    return out;
}

void pb_input_poll(void)
{
    s_prev = s_cur;
    s_cur  = pb_input_read();
}

int pb_input_held(uint8_t btn)
{
    return (s_cur & btn) != 0;
}

int pb_input_just_pressed(uint8_t btn)
{
    return ((s_cur & btn) != 0) && ((s_prev & btn) == 0);
}

int pb_input_just_released(uint8_t btn)
{
    return ((s_cur & btn) == 0) && ((s_prev & btn) != 0);
}
