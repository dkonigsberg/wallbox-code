#ifndef USER_SONOS_LISTENER_H
#define USER_SONOS_LISTENER_H

#include "user_sonos_client.h"

typedef enum transport_state_t {
    UNKNOWN = 0,
    PLAYING,
    STOPPED,
    PAUSED_PLAYBACK
} transport_state_t;

typedef struct sonos_notify_info {
    char subscribe_id[64];
    transport_state_t transport_state;
    int number_of_tracks;
    int current_track;
} sonos_notify_info;

typedef void (* user_sonos_listener_callback_t)(
    const sonos_notify_info *info,
    void *user_data);

void user_sonos_listener_init(void);

void user_sonos_listener_set_callback(user_sonos_listener_callback_t callback, void *user_data);

void user_sonos_listener_subscribe(const sonos_device *device);

#endif /* USER_SONOS_LISTENER_H */