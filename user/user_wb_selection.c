#include "user_wb_selection.h"

#include <ets_sys.h>
#include <osapi.h>
#include <gpio.h>
#include <os_type.h>
#include <driver/uart.h>
#include <user_interface.h>

#include "user_sonos_client.h"

/* Definition of GPIO pin parameters */
#define SELECTION_SIGNAL_IO_MUX  PERIPHS_IO_MUX_GPIO4_U
#define SELECTION_SIGNAL_IO_NUM  4
#define SELECTION_SIGNAL_IO_FUNC FUNC_GPIO4

/* Maximum accumulated length of a pulse stream */
#define MAX_WB_SELECTION_PULSES 64

typedef struct wb_selection_pulse {
    uint32 elapsed; // time since the end of the last pulse
    uint32 duration; // duration of the current pulse
} wb_selection_pulse;

LOCAL void wb_pulse_list_clear();
LOCAL bool wb_pulse_list_tally_3w1_100(char *letter, int *number);
LOCAL bool wb_pulse_list_tally_v3wa_200(char *letter, int *number);
LOCAL void wp_pulse_gpio_intr_handler(int *dummy);
LOCAL void wb_pulse_timer_func(void *arg);

LOCAL volatile wallbox_type wb_selected_type;
LOCAL volatile wallbox_type wb_active_type;
LOCAL volatile int wb_pulse_last_value;
LOCAL volatile uint32 wb_pulse_last_time;
LOCAL volatile wb_selection_pulse wb_pulse_list[MAX_WB_SELECTION_PULSES];
LOCAL volatile int wb_pulse_index;
LOCAL volatile os_timer_t wb_pulse_timer;

void wb_pulse_list_clear()
{
    wb_pulse_index = 0;
    os_memset(wb_pulse_list, 0, sizeof(wb_pulse_list));
}

/*
 * Count the signal pulses.
 * Wallbox: Seeburg Wall-O-Matic 3W-1 100
 */
bool wb_pulse_list_tally_3w1_100(char *letter, int *number)
{
    int i;
    int p1 = 0;
    int p2 = 0;
    bool delimiter = false;
    char letter_val;
    int number_val;

    for (i = 0; i < wb_pulse_index; i++) {
        // Pulse delimiter is ~800ms, so use 500 to be safe
        if (p1 > 0 && !delimiter && wb_pulse_list[i].duration > 500000) {
            delimiter = true;
            //os_printf("----DELIMITER (PULSE)----\r\n");
        }
        else {
            // Gap delimiter is ~170ms, so use 100 to be safe
            if (p1 > 0 && !delimiter && wb_pulse_list[i].elapsed > 100000) {
                delimiter = true;
                //os_printf("----DELIMITER (GAP)----\r\n");
            }
            if (!delimiter) {
                p1++;
            }
            else {
                p2++;
            }
        }
    }

    if (p2 < 1 || p2 > 5) {
        // Reject invalid or incomplete values
        return false;
    }

    if (p1 >= 1 && p1 <= 10) {
        number_val = p1;
        letter_val = 'A' + (p2 - 1) * 2;
    }
    else if (p1 >= 12 && p1 <= 21) {
        number_val = p1 - 11;
        letter_val = 'A' + ((p2 - 1) * 2) + 1;
    }
    else {
        // Reject invalid pulse 1 value
        return false;
    }

    // This wallbox skips the letter 'I'
    if (letter_val > 'H') { letter_val++; }

    if (letter && number) {
        *letter = letter_val;
        *number = number_val;
    }

    return true;
}

/*
 * Count the signal pulses.
 * Wallbox: Seeburg Wall-O-Matic V-3WA 200
 */
LOCAL bool wb_pulse_list_tally_v3wa_200(char *letter, int *number)
{
    LOCAL const char WB_LETTERS[] = "ABCDEFGHJKLMNPQRSTUV";
    int i;
    int p1 = 0;
    int p2 = 0;
    bool delimiter = false;
    char letter_val;
    int number_val;

    for (i = 0; i < wb_pulse_index; i++) {
        // Gap delimiter is ~230ms, so use 125 to be safe
        if (p1 > 0 && !delimiter && wb_pulse_list[i].elapsed > 125000) {
            delimiter = true;
            //os_printf("----DELIMITER (GAP)----\r\n");
        }
        if (!delimiter) {
            p1++;
        }
        else {
            p2++;
        }
    }

    if (p1 < 2 || p1 > 21 || p2 < 1 || p2 > 10) {
        // Reject invalid or incomplete values
        return false;
    }

    letter_val = WB_LETTERS[p1 - 2];
    number_val = p2;

    if (letter && number) {
        *letter = letter_val;
        *number = number_val;
    }

    return true;
}

LOCAL void wp_pulse_gpio_intr_handler(int *dummy)
{
    uint32 gpio_status = GPIO_REG_READ(GPIO_STATUS_ADDRESS);

    // If the interrupt was by GPIO4
    if (gpio_status & BIT(SELECTION_SIGNAL_IO_NUM)) {
        uint32_t timeout = 3000;

        // Disable interrupt for GPIO4
        gpio_pin_intr_state_set(
            GPIO_ID_PIN(SELECTION_SIGNAL_IO_NUM),
            GPIO_PIN_INTR_DISABLE);

        int currentPulseValue = GPIO_INPUT_GET(GPIO_ID_PIN(SELECTION_SIGNAL_IO_NUM));
        uint32 currentPulseTime = system_get_time();

        if (wb_active_type != wb_selected_type) {
            wb_active_type = wb_selected_type;
            if (wb_pulse_index > 0) {
                wb_pulse_list_clear();
            }            
        }

        // Do something
        if(currentPulseValue != wb_pulse_last_value) {
            os_timer_disarm(&wb_pulse_timer);

            uint32 elapsed = currentPulseTime - wb_pulse_last_time;
            if (currentPulseValue == 1) {
                //os_printf("--> Gap: %dms\r\n", (elapsed / 1000));
                if (wb_pulse_list[wb_pulse_index].duration == 0) {
                    wb_pulse_list[wb_pulse_index].elapsed = elapsed;
                } else {
                    // Error
                    os_printf("--> Error 1\r\n");
                }
            }
            else if (currentPulseValue == 0) {
                //os_printf("--> Pulse: %dms\r\n", (elapsed / 1000));
                if (wb_pulse_list[wb_pulse_index].elapsed > 0) {
                    wb_pulse_list[wb_pulse_index].duration = elapsed;
                    wb_pulse_index++;
                } else {
                    // Error
                    os_printf("--> Error 2\r\n");
                }
            }
            if (wb_pulse_index >= MAX_WB_SELECTION_PULSES) {
                os_printf("--> Error MAX\r\n");
                wb_pulse_list_clear();
            } else {
                // Count existing pulses
                if (wb_active_type == SEEBURG_3W1_100) {
                    if (wb_pulse_list_tally_3w1_100(0, 0)) {
                        timeout = 250;
                    }
                } else if(wb_active_type == SEEBURG_V3WA_200) {
                    if (wb_pulse_list_tally_v3wa_200(0, 0)) {
                        timeout = 250;
                    }
                } else {
                    // Unknown wallbox type
                    os_printf("Unknown wallbox\n");
                    wb_pulse_list_clear();
                }
            }
            wb_pulse_last_value = currentPulseValue;
            wb_pulse_last_time = currentPulseTime;

            os_timer_setfn(&wb_pulse_timer, (os_timer_func_t *)wb_pulse_timer_func, /*arg*/NULL);
            os_timer_arm(&wb_pulse_timer, timeout, 0);
        }

        // Clear interrupt status for GPIO4
        GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, gpio_status & BIT(SELECTION_SIGNAL_IO_NUM));

        // Reactivate interrupts for GPIO4
        gpio_pin_intr_state_set(
            GPIO_ID_PIN(SELECTION_SIGNAL_IO_NUM),
            GPIO_PIN_INTR_ANYEDGE);
    }
}

void wb_pulse_timer_func(void *arg)
{
    char letter;
    int number;
    bool result;

    os_timer_disarm(&wb_pulse_timer);

    gpio_pin_intr_state_set(GPIO_ID_PIN(4), GPIO_PIN_INTR_DISABLE);

    if (wb_active_type == SEEBURG_3W1_100) {
        result = wb_pulse_list_tally_3w1_100(&letter, &number);
    } else if(wb_active_type == SEEBURG_V3WA_200) {
        result = wb_pulse_list_tally_v3wa_200(&letter, &number);
    } else {
        result = false;
    }

    wb_pulse_list_clear();
    gpio_pin_intr_state_set(GPIO_ID_PIN(4), GPIO_PIN_INTR_ANYEDGE);

    if (result) {
        os_printf("--> Song: %c%d\r\n", letter, number);
        user_sonos_client_enqueue(letter, number);
    } else {
        os_printf("--> Timeout decode error\r\n");
    }
}

void ICACHE_FLASH_ATTR user_wb_set_wallbox_type(wallbox_type wb_type)
{
    wb_selected_type = wb_type;
}

void ICACHE_FLASH_ATTR user_wb_selection_init(void)
{
    // Initialize state variables
    wb_pulse_last_value = 0;
    wb_pulse_last_time = system_get_time();
    os_memset(wb_pulse_timer, 0, sizeof(wb_pulse_timer));
    wb_pulse_list_clear();
    wb_selected_type = UNKNOWN_WALLBOX;
    wb_active_type = UNKNOWN_WALLBOX;

    // Disable interrupts by GPIO
    ETS_GPIO_INTR_DISABLE();

    // Set signal pin as GPIO
    PIN_FUNC_SELECT(SELECTION_SIGNAL_IO_MUX, SELECTION_SIGNAL_IO_FUNC);
    PIN_PULLUP_DIS(SELECTION_SIGNAL_IO_MUX);

    // Set signal pin as input
    gpio_output_set(0, 0, 0, GPIO_ID_PIN(SELECTION_SIGNAL_IO_NUM));

    // Attach interrupt handler
    ETS_GPIO_INTR_ATTACH(wp_pulse_gpio_intr_handler, /*arg*/NULL);

    // Set GPIO register
    gpio_register_set(GPIO_PIN_ADDR(SELECTION_SIGNAL_IO_NUM),
                      GPIO_PIN_INT_TYPE_SET(GPIO_PIN_INTR_DISABLE)  |
                      GPIO_PIN_PAD_DRIVER_SET(GPIO_PAD_DRIVER_DISABLE) |
                      GPIO_PIN_SOURCE_SET(GPIO_AS_PIN_SOURCE));

    // Clear GPIO status
    GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, BIT(SELECTION_SIGNAL_IO_NUM));

    // Enable interrupt on any edge transition
    gpio_pin_intr_state_set(
        GPIO_ID_PIN(SELECTION_SIGNAL_IO_NUM),
        GPIO_PIN_INTR_ANYEDGE);

    // Enable interrupts by GPIO
    ETS_GPIO_INTR_ENABLE();
}
