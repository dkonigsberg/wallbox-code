#ifndef USER_CONFIG_H
#define USER_CONFIG_H

#include <user_interface.h>

typedef enum wallbox_type {
    UNKNOWN_WALLBOX = 0,
    SEEBURG_3W1_100,
    SEEBURG_V3WA_200,
    MAX_WALLBOX_TYPES
} wallbox_type;

void user_config_init(void);

void user_config_set_wallbox_type(wallbox_type wallbox);
wallbox_type user_config_get_wallbox_type();

void user_config_set_sonos_uuid(const char *uuid);
const char* user_config_get_sonos_uuid();

void user_config_set_sonos_uri_base(const char *uri_base);
const char* user_config_get_sonos_uri_base();

void user_config_set_sonos_track_files(char (*track_file)[200][16]);
const char* user_config_get_sonos_track_file(int index);

#endif /* USER_CONFIG_H */