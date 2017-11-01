#ifndef USER_SONOS_CLIENT_H
#define USER_SONOS_CLIENT_H

#include <os_type.h>

typedef struct sonos_device {
    uint8 ip[4];
    int port;
    char uuid[64];
    char zone_name[128];
} sonos_device;

void user_sonos_client_init(void);
bool user_sonos_client_set_device(const char *uuid);
bool user_sonos_client_get_device(sonos_device *device_info);
void user_sonos_client_enqueue(char letter, int number);

#endif /* USER_SONOS_CLIENT_H */