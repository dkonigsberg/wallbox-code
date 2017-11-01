#ifndef USER_SONOS_REQUEST_H
#define USER_SONOS_REQUEST_H

#include "user_sonos_client.h"

typedef struct sonos_add_uri_info {
    int first_track_num_enqueued;
    int num_tracks_added;
    int new_queue_length;
} sonos_add_uri_info;

typedef struct sonos_position_info {
    int track;
    int track_duration;
    char track_uri[256];
    int rel_time;
} sonos_position_info;

typedef struct sonos_subscribe_info {
    char subscribe_id[64];
    int timeout_secs;
} sonos_subscribe_info;

typedef void (* user_sonos_request_callback_t)(
    void *user_data, bool success);
typedef void (* user_sonos_request_add_uri_callback_t)(
    const sonos_add_uri_info *info,
    void *user_data, bool success);
typedef void (* user_sonos_request_position_callback_t)(
    const sonos_position_info *info,
    void *user_data, bool success);
typedef void (* user_sonos_request_subscribe_callback_t)(
    const sonos_subscribe_info *info,
    void *user_data, bool success);

void user_sonos_request_init(void);

bool user_sonos_request_add_uri(const sonos_device *device, const char *uri,
    user_sonos_request_add_uri_callback_t callback, void *user_data);

bool user_sonos_request_set_transport(const sonos_device *device,
    user_sonos_request_callback_t callback, void *user_data);

bool user_sonos_request_seek_track(const sonos_device *device, int track,
    user_sonos_request_callback_t callback, void *user_data);

bool user_sonos_request_play(const sonos_device *device,
    user_sonos_request_callback_t callback, void *user_data);

bool user_sonos_request_get_position_info(const sonos_device *device,
    user_sonos_request_position_callback_t callback, void *user_data);

bool user_sonos_request_subscribe(const sonos_device *device,
    uint8 listener_ip[4], int listener_port, int timeout_secs,
    user_sonos_request_subscribe_callback_t callback, void *user_data);

bool user_sonos_request_resubscribe(const sonos_device *device,
    const char *subscribe_id, int timeout_secs,
    user_sonos_request_subscribe_callback_t callback, void *user_data);

#endif /* USER_SONOS_REQUEST_H */