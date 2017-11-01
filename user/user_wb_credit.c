#include "user_wb_credit.h"

#include <ets_sys.h>
#include <osapi.h>
#include <gpio.h>
#include <os_type.h>
#include <user_interface.h>

LOCAL void ICACHE_FLASH_ATTR credit_coin_clear(void);

LOCAL uint8 credit_lock = 0;
LOCAL os_timer_t credit_timer;

void ICACHE_FLASH_ATTR user_wb_credit_init(void)
{
    PIN_FUNC_SELECT(COIN_NICKEL_IO_MUX, COIN_NICKEL_IO_FUNC);
    PIN_PULLUP_DIS(COIN_NICKEL_IO_MUX);

    PIN_FUNC_SELECT(COIN_DIME_IO_MUX, COIN_DIME_IO_FUNC);
    PIN_PULLUP_DIS(COIN_DIME_IO_MUX);

    PIN_FUNC_SELECT(COIN_QUARTER_IO_MUX, COIN_QUARTER_IO_FUNC);
    PIN_PULLUP_DIS(COIN_QUARTER_IO_MUX);

    gpio_output_set(0,
        GPIO_ID_PIN(COIN_NICKEL_IO_NUM) | GPIO_ID_PIN(COIN_DIME_IO_NUM) | GPIO_ID_PIN(COIN_QUARTER_IO_NUM),
        GPIO_ID_PIN(COIN_NICKEL_IO_NUM) | GPIO_ID_PIN(COIN_DIME_IO_NUM) | GPIO_ID_PIN(COIN_QUARTER_IO_NUM),
        0);
}

int ICACHE_FLASH_ATTR user_wb_credit_drop(CoinType coin_type)
{
    int coin_gpio = -1;
    uint32_t coin_timer = 0;

    switch (coin_type) {
    case NICKEL:
        coin_gpio = COIN_NICKEL_IO_NUM;
        coin_timer = COIN_NICKEL_PULSE_MS;
        break;
    case DIME:
        coin_gpio = COIN_DIME_IO_NUM;
        coin_timer = COIN_DIME_PULSE_MS;
        break;
    case QUARTER:
        coin_gpio = COIN_QUARTER_IO_NUM;
        coin_timer = COIN_QUARTER_PULSE_MS;
        break;
    default:
        return -1;
    }

    if (credit_lock == 1) {
        return 0;
    }

    credit_lock = 1;
    GPIO_OUTPUT_SET(GPIO_ID_PIN(coin_gpio), 1);
    os_timer_disarm(&credit_timer);
    os_timer_setfn(&credit_timer, (os_timer_func_t *)credit_coin_clear, NULL);
    os_timer_arm(&credit_timer, coin_timer, 0);
    return 1;
}

LOCAL void ICACHE_FLASH_ATTR credit_coin_clear(void)
{
    GPIO_OUTPUT_SET(GPIO_ID_PIN(COIN_NICKEL_IO_NUM), 0);
    GPIO_OUTPUT_SET(GPIO_ID_PIN(COIN_DIME_IO_NUM), 0);
    GPIO_OUTPUT_SET(GPIO_ID_PIN(COIN_QUARTER_IO_NUM), 0);
    credit_lock = 0;
}
