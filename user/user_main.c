#include <ets_sys.h>
#include <osapi.h>
#include <gpio.h>
#include <os_type.h>
#include <driver/uart.h>
#include <user_interface.h>

#include "user_config.h"
#include "user_wb_credit.h"
#include "user_wb_selection.h"
#include "user_webserver.h"
#include "user_sonos_discovery.h"
#include "user_sonos_listener.h"
#include "user_sonos_request.h"
#include "user_sonos_client.h"

/* Definition of GPIO pin parameters */
#define CONFIG_SIGNAL_TO_MUX  PERIPHS_IO_MUX_MTCK_U
#define CONFIG_SIGNAL_TO_NUM  13
#define CONFIG_SIGNAL_IO_FUNC FUNC_GPIO13

LOCAL bool config_mode;

LOCAL void ICACHE_FLASH_ATTR log_system_info()
{
    enum flash_size_map size_map = system_get_flash_size_map();

    os_printf("Firmware build: %s\n", BUILD_DESCRIBE);
    os_printf("SDK version: %s\n", system_get_sdk_version());

    os_printf("Flash size: ");
    switch(size_map) {
    case FLASH_SIZE_4M_MAP_256_256:
        os_printf("4Mbit (256KB+256KB)");
        break;
    case FLASH_SIZE_2M:
        os_printf("2Mbit");
        break;
    case FLASH_SIZE_8M_MAP_512_512:
        os_printf("8Mbit (512KB+512KB)");
        break;
    case FLASH_SIZE_16M_MAP_512_512:
        os_printf("16Mbit (512KB+512KB)");
        break;
    case FLASH_SIZE_32M_MAP_512_512:
        os_printf("32Mbit (512KB+512KB)");
        break;
    case FLASH_SIZE_16M_MAP_1024_1024:
        os_printf("16Mbit (1024KB+1024KB)");
        break;
    case FLASH_SIZE_32M_MAP_1024_1024:
        os_printf("32Mbit (1024KB+1024KB)");
        break;
    case FLASH_SIZE_32M_MAP_2048_2048:
        os_printf("32Mbit (2024KB+2024KB)");
        break;
    case FLASH_SIZE_64M_MAP_1024_1024:
        os_printf("64Mbit (1024KB+1024KB)");
        break;
    case FLASH_SIZE_128M_MAP_1024_1024:
        os_printf("128Mbit (1024KB+1024KB)");
    default:
        os_printf("Unknown");
        break;
    }
    os_printf("\n");

    uint8 macaddr[6];
    if (wifi_get_macaddr(STATION_IF, macaddr)) {
        os_printf("MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
            macaddr[0], macaddr[1], macaddr[2],
            macaddr[3], macaddr[4], macaddr[5]);
    }

    system_print_meminfo();
}

LOCAL void ICACHE_FLASH_ATTR user_wifi_event_handler(System_Event_t *evt)
{
    if (evt->event == EVENT_STAMODE_GOT_IP) {
        user_sonos_discovery_start();
    }
    else if (evt->event == EVENT_STAMODE_DISCONNECTED) {
        user_sonos_discovery_abort();
    }
}

LOCAL void ICACHE_FLASH_ATTR user_sonos_discovery_callback(
    const sonos_device *device, void *user_data)
{
    const char *selected_uuid = user_config_get_sonos_uuid();

    if (os_strcmp(device->uuid, selected_uuid) == 0) {
        user_sonos_client_set_device(selected_uuid);
    }
}

void ICACHE_FLASH_ATTR user_main_gpio_init()
{
    gpio_init();
    PIN_FUNC_SELECT(CONFIG_SIGNAL_TO_MUX, CONFIG_SIGNAL_IO_FUNC);
    PIN_PULLUP_EN(CONFIG_SIGNAL_TO_MUX);
    gpio_output_set(0, 0, 0, GPIO_ID_PIN(CONFIG_SIGNAL_TO_NUM));
}

void ICACHE_FLASH_ATTR user_main_wifi_init()
{
    if (config_mode) {
        struct softap_config config;
        uint8 macaddr[6];

        // Disable Wi-Fi auto-connect
        if (!wifi_station_set_auto_connect(0)) {
            os_printf("wifi_station_set_auto_connect error\n");
            return;
        }

        // Switch Wi-Fi to STA+AP mode temporarily
        if (!wifi_set_opmode_current(STATIONAP_MODE)) {
            os_printf("wifi_set_opmode error\n");
            return;
        }
        
        // Get the existing AP configuration
        if (!wifi_softap_get_config(&config)) {
            os_printf("wifi_softap_get_config error\n");
            return;
        }

        // Get the MAC address of the station
        if (!wifi_get_macaddr(STATION_IF, macaddr)) {
            os_printf("wifi_get_macaddr error\n");
            return;
        }

        // Construct an obvious SSID based on the device-specific
        // part of the MAC address.
        config.ssid_len = os_sprintf((char *)config.ssid,
            "Wallbox %02X%02X%02X",
            macaddr[3], macaddr[4], macaddr[5]);

        // Make this AP completely open, since its only supposed to
        // exist for a brief time during setup mode.
        os_bzero(&config.password, sizeof(config.password));
        config.authmode = AUTH_OPEN;
        config.ssid_hidden = 0;

        if (!wifi_softap_set_config(&config)) {
            os_printf("wifi_softap_set_config error\n");
            return;
        }
    }
    else {
        // Make sure Wi-Fi is in regular STA mode
        if (wifi_get_opmode() != STATION_MODE) {
            if (!wifi_set_opmode(STATION_MODE)) {
                os_printf("wifi_set_opmode error\n");
            }
        }
        // Enable auto-connect
        if (!wifi_station_set_auto_connect(1)) {
            os_printf("wifi_station_set_auto_connect error\n");
        }
        // Enable auto-reconnect
        if (!wifi_station_set_reconnect_policy(true)) {
            os_printf("wifi_station_set_reconnect_policy error\n");
        }
    }
}

void ICACHE_FLASH_ATTR user_rf_pre_init(void)
{
}

void ICACHE_FLASH_ATTR user_init()
{
    config_mode = false;

    // Initialize the serial port
    uart_init(BIT_RATE_115200, BIT_RATE_115200);
    //uart_init(BIT_RATE_74880, BIT_RATE_74880);

    // Configure the basic GPIO pins
    user_main_gpio_init();

    os_printf("\n\n"); 
    os_delay_us(UINT16_MAX);
    os_printf("\n\n"); 
    os_printf("----------------------\n");
    os_printf("Wall-O-Matic Interface\n");
    os_printf("----------------------\n\n");
    log_system_info();

    if (GPIO_INPUT_GET(GPIO_ID_PIN(CONFIG_SIGNAL_TO_NUM)) == 0) {
        config_mode = true;
        os_printf("Starting in configuration mode.\n");
    }

    // Initialize the persistent settings data
    user_config_init();

    // Use GPIO2 as the WiFi status LED
    wifi_status_led_install(2, PERIPHS_IO_MUX_GPIO2_U, FUNC_GPIO2);

    // Initialize Wi-Fi based on the startup mode
    user_main_wifi_init();

    // Initialize the application components if we're not in
    // Wi-Fi configuration mode.
    if (!config_mode) {
        user_wb_credit_init();
        user_wb_selection_init();
        user_sonos_discovery_init();
        user_sonos_listener_init();
        user_sonos_request_init();
        user_sonos_client_init();

        user_wb_set_wallbox_type(user_config_get_wallbox_type());

        wifi_set_event_handler_cb(user_wifi_event_handler);
        user_sonos_discovery_set_callback(user_sonos_discovery_callback, 0);
    }

    // Always initialize the web server component
    user_webserver_init(SERVER_PORT, config_mode);
}
