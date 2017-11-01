#ifndef USER_SONOS_DISCOVERY_H
#define USER_SONOS_DISCOVERY_H

#include "user_sonos_client.h"

typedef void (* user_sonos_discovery_callback_t)(
    const sonos_device *device, void *user_data);

void user_sonos_discovery_init(void);
void user_sonos_discovery_start(void);
void user_sonos_discovery_abort(void);
void user_sonos_discovery_set_callback(user_sonos_discovery_callback_t callback, void *user_data);
int user_sonos_discovery_get_device_by_uuid(sonos_device *device, const char *uuid);
int user_sonos_discovery_get_device_by_name(sonos_device *device, const char *zone_name);
bool user_sonos_discovery_json_devices(char **json_data);

#endif /* USER_SONOS_DISCOVERY_H */