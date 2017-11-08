#include "user_config.h"

#include <ets_sys.h>
#include <osapi.h>
#include <user_interface.h>

#define ESP_PARAM_VERSION 1
#define ESP_PARAM_START_SEC 0x6C

/*
 * Saved parameter struct.
 * Padded out to the maximum size of 4096 bytes using
 * reserved fields.
 */
struct esp_saved_param_t {
    uint8 version;
    uint8 wallbox_type;
    uint8 reserved0[254];
    char sonos_uuid[64];
    uint8 sonos_reserved[320];
    char sonos_uri_base[256];
    char sonos_track_file[200][16];
};

LOCAL struct esp_saved_param_t esp_param;

void ICACHE_FLASH_ATTR user_config_init(void)
{
    os_bzero(&esp_param, sizeof(esp_param));

    // Try to load existing parameter data
    if (!system_param_load(ESP_PARAM_START_SEC, 0, &esp_param, sizeof(esp_param))) {
        os_printf("system_param_load error\n");
    }

    os_printf("Loaded param data, version=%d\n", esp_param.version);

    // Initialize the saved data if its new or invalid
    if (esp_param.version == 0 || esp_param.version > ESP_PARAM_VERSION) {
        if (esp_param.version == 0) {
            os_printf("Param data is new, initializing...\n");
        } else if (esp_param.version > ESP_PARAM_VERSION) {
            os_printf("Param data is corrupted, reinitializing...\n");
        }

        // Prepare a clean version of the struct
        os_bzero(&esp_param, sizeof(esp_param));
        esp_param.version = ESP_PARAM_VERSION;

        if (!system_param_save_with_protect(ESP_PARAM_START_SEC, &esp_param, sizeof(esp_param))) {
            os_printf("system_param_save_with_protect error\n");
        }
    }
}

void ICACHE_FLASH_ATTR user_config_set_wallbox_type(wallbox_type wallbox)
{
    if (wallbox < UNKNOWN_WALLBOX || wallbox >= MAX_WALLBOX_TYPES) {
        os_printf("Invalid wallbox type\n");
        return;
    }

    esp_param.wallbox_type = (uint8)wallbox;

    if (!system_param_save_with_protect(ESP_PARAM_START_SEC, &esp_param, sizeof(esp_param))) {
        os_printf("system_param_save_with_protect error\n");
    }
}

wallbox_type ICACHE_FLASH_ATTR user_config_get_wallbox_type()
{
    wallbox_type wallbox = (wallbox_type)esp_param.wallbox_type;
    if (wallbox >= UNKNOWN_WALLBOX && wallbox < MAX_WALLBOX_TYPES) {
        return wallbox;
    } else {
        return UNKNOWN_WALLBOX;
    }
}

void ICACHE_FLASH_ATTR user_config_set_sonos_uuid(const char *uuid)
{
    if (uuid && os_strlen(uuid) > sizeof(esp_param.sonos_uuid) - 1) {
        os_printf("UUID too long\n");
        return;
    }

    os_bzero(esp_param.sonos_uuid, sizeof(esp_param.sonos_uuid));

    if (uuid && uuid[0] != '\0') {
        os_strcpy(esp_param.sonos_uuid, uuid);
    }

    if (!system_param_save_with_protect(ESP_PARAM_START_SEC, &esp_param, sizeof(esp_param))) {
        os_printf("system_param_save_with_protect error\n");
    }
}

const char* ICACHE_FLASH_ATTR user_config_get_sonos_uuid()
{
    return esp_param.sonos_uuid;
}

void ICACHE_FLASH_ATTR user_config_set_sonos_uri_base(const char *uri_base)
{
    if (uri_base && os_strlen(uri_base) > sizeof(esp_param.sonos_uri_base) - 1) {
        os_printf("URI base too long\n");
        return;
    }

    os_bzero(esp_param.sonos_uri_base, sizeof(esp_param.sonos_uri_base));

    if (uri_base && uri_base[0] != '\0') {
        os_strcpy(esp_param.sonos_uri_base, uri_base);
    }

    if (!system_param_save_with_protect(ESP_PARAM_START_SEC, &esp_param, sizeof(esp_param))) {
        os_printf("system_param_save_with_protect error\n");
    }
}

const char* ICACHE_FLASH_ATTR user_config_get_sonos_uri_base()
{
    return esp_param.sonos_uri_base;
}

void ICACHE_FLASH_ATTR user_config_set_sonos_track_files(char (*track_file)[200][16])
{
    int i;
    for (i = 0; i < 200; i++) {
        os_strncpy(esp_param.sonos_track_file[i], (*track_file)[i], 16);
        esp_param.sonos_track_file[i][15] = '\0';
    }

    if (!system_param_save_with_protect(ESP_PARAM_START_SEC, &esp_param, sizeof(esp_param))) {
        os_printf("system_param_save_with_protect error\n");
    }
}

const char* ICACHE_FLASH_ATTR user_config_get_sonos_track_file(int index)
{
    if (index < 0 || index > 199) {
        return NULL;
    }
    return esp_param.sonos_track_file[index];
}
