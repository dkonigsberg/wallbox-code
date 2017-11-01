#include "user_sonos_client.h"

#include <ets_sys.h>
#include <os_type.h>
#include <osapi.h>
#include <mem.h>
#include <user_interface.h>
#include <espconn.h>

#include "user_sonos_discovery.h"
#include "user_sonos_listener.h"
#include "user_sonos_request.h"

typedef struct sonos_enqueue_data {
    sonos_device device;
    uint32 start_time;
    int num_enqueued;
} sonos_enqueue_data;

LOCAL void ICACHE_FLASH_ATTR sonos_listener_callback(const sonos_notify_info *info, void *user_data);
LOCAL void ICACHE_FLASH_ATTR sonos_add_uri_callback(const sonos_add_uri_info *info, void *user_data, bool success);
LOCAL void ICACHE_FLASH_ATTR sonos_position_callback(const sonos_position_info *info, void *user_data, bool success);
LOCAL void ICACHE_FLASH_ATTR sonos_set_transport_callback(void *user_data, bool success);
LOCAL void ICACHE_FLASH_ATTR sonos_seek_callback(void *user_data, bool success);
LOCAL void ICACHE_FLASH_ATTR sonos_play_callback(void *user_data, bool success);
LOCAL void ICACHE_FLASH_ATTR sonos_enqueue_cleanup(sonos_enqueue_data *enqueue_data);
LOCAL bool ICACHE_FLASH_ATTR uuid_sid_match(const char *uuid, const char *sid);

LOCAL sonos_device device;
LOCAL sonos_notify_info device_notify_info;
LOCAL uint32 device_notify_time = 0;
LOCAL bool device_set = false;
LOCAL bool enqueue_lock = false;

void ICACHE_FLASH_ATTR user_sonos_client_init(void)
{
    os_memset(&device, 0, sizeof(sonos_device));
    os_memset(&device_notify_info, 0, sizeof(sonos_notify_info));
    device_notify_time = 0;
}

bool ICACHE_FLASH_ATTR user_sonos_client_set_device(const char *uuid)
{
    if (device_set && os_strcmp(device.uuid, uuid) == 0) {
        os_printf("Device already selected\n");
        return false;
    }
    
    if (enqueue_lock) {
        os_printf("Cannot set device while enqueue in progress\n");
        return false;
    }

    int result = user_sonos_discovery_get_device_by_uuid(&device, uuid);
    if (result == 1) {
        os_printf("Device selected: \"%s\" -> " IPSTR ":%d\n",
            device.zone_name, IP2STR(device.ip), device.port);
        device_set = true;
    } else if (result == 0) {
        os_printf("Unable to find device: \"%s\"\n", uuid);
    } else {
        os_printf("Device selection error\n");
    }

    if (device_set) {
        os_memset(&device_notify_info, 0, sizeof(sonos_notify_info));
        device_notify_time = 0;
        user_sonos_listener_subscribe(&device);
        user_sonos_listener_set_callback(sonos_listener_callback, NULL);
    }

    return device_set;
}

bool user_sonos_client_get_device(sonos_device *device_info)
{
    if (!device_set || !device_info) {
        return false;
    }

    os_memcpy(device_info, &device, sizeof(sonos_device));
    return true;
}

void ICACHE_FLASH_ATTR user_sonos_client_enqueue(char letter, int number)
{
    LOCAL const char URI_SCHEME[] = "x-file-cifs:";
    char uri_buf[512];
    int n = 0;

    if (!device_set) {
        os_printf("Device not selected\n");
        return;
    }

    if (enqueue_lock) {
        os_printf("Track enqueue in progress\n");
        return;
    }

    const char *uri_base = user_config_get_sonos_uri_base();
    if (!uri_base || os_strlen(uri_base) == 0) {
        os_printf("No URI base configured\n");
        return;
    }

    const int track_index = wb_selection_to_index(letter, number);
    if (track_index < 0) {
        os_printf("Invalid track selection\n");
        return;
    }

    const char *track_file = user_config_get_sonos_track_file(track_index);
    if (!track_file || os_strlen(track_file) == 0) {
        os_printf("No configured track file\n");
        return;
    }

    sonos_enqueue_data *enqueue_data = (sonos_enqueue_data *)os_zalloc(sizeof(sonos_enqueue_data));
    if (!enqueue_data) {
        return;
    }

    os_memcpy(&enqueue_data->device, &device, sizeof(sonos_device));
    enqueue_data->start_time = system_get_time();

    // Combine the URI elements into a complete URI
    os_strcpy(uri_buf, URI_SCHEME);
    n += os_strlen(URI_SCHEME);
    os_strcpy(uri_buf + n, uri_base);
    n += os_strlen(uri_base);
    if (uri_buf[n - 1] != '/' && uri_buf[n - 1] != '\\') {
        uri_buf[n++] = '/';
    }
    os_strcpy(uri_buf + n, track_file);

    os_printf("Enqueue URI: \"%s\"\n", uri_buf);

    user_sonos_request_add_uri(&enqueue_data->device, uri_buf,
        sonos_add_uri_callback, enqueue_data);

    enqueue_lock = true;
}

LOCAL void ICACHE_FLASH_ATTR sonos_listener_callback(const sonos_notify_info *info, void *user_data)
{
    os_printf("sonos_listener_callback\n");
    if (!info) { return; }

    if (uuid_sid_match(device.uuid, info->subscribe_id)) {
        os_memcpy(&device_notify_info, info, sizeof(sonos_notify_info));
        device_notify_time = system_get_time();
    }
}

LOCAL void ICACHE_FLASH_ATTR sonos_add_uri_callback(const sonos_add_uri_info *info, void *user_data, bool success)
{
    sonos_enqueue_data *enqueue_data = (sonos_enqueue_data *)user_data;
    os_printf("sonos_add_uri_callback, success=%d\n", success);

    if (!success || !info || !enqueue_data) {
        sonos_enqueue_cleanup(enqueue_data);
        return;
    }

    #if 1
    os_printf("AddURIToQueue\n");
    os_printf(" first_track_num_enqueued=%d\n", info->first_track_num_enqueued);
    os_printf(" num_tracks_added=%d\n", info->num_tracks_added);
    os_printf(" new_queue_length=%d\n", info->new_queue_length);
    #endif
    enqueue_data->num_enqueued = info->first_track_num_enqueued;

    user_sonos_request_get_position_info(&enqueue_data->device,
        sonos_position_callback, enqueue_data);
}

LOCAL void ICACHE_FLASH_ATTR sonos_position_callback(const sonos_position_info *info, void *user_data, bool success)
{
    sonos_enqueue_data *enqueue_data = (sonos_enqueue_data *)user_data;
    os_printf("sonos_position_callback, success=%d\n", success);

    if (!success || !info || !enqueue_data) {
        sonos_enqueue_cleanup(enqueue_data);
        return;
    }

    // Inspect the position info, to decide whether or not we need
    // to set the transport.
    bool need_set_transport = true;
    #if 1
    os_printf("GetPositionInfo\n");
    os_printf(" track=%d\n", info->track);
    os_printf(" track_duration=%d\n", info->track_duration);
    os_printf(" track_uri=\"%s\"\n", info->track_uri);
    os_printf(" rel_time=%d\n", info->rel_time);
    #endif

    // No need to set the transport if we're currently on the local file
    // share selection.
    int uri_len = os_strlen(info->track_uri);
    if (uri_len > 12 && os_strncmp(info->track_uri, "x-file-cifs:", 12) == 0) {
        need_set_transport = false;
    }
    
    if (need_set_transport) {
        user_sonos_request_set_transport(&enqueue_data->device,
            sonos_set_transport_callback, enqueue_data);
    } else {
        // Not currently playing
        if (info->track == 0 && info->track_duration == 0 && info->rel_time == 0) {
            user_sonos_request_play(&enqueue_data->device,
                sonos_play_callback, enqueue_data);
        }
        // On added track, likely not currently playing
        else if (info->track == enqueue_data->num_enqueued && info->rel_time == 0) {
            user_sonos_request_play(&enqueue_data->device,
                sonos_play_callback, enqueue_data);
        }
        // On a previous track, likely not currently playing
        else if(info->track < enqueue_data->num_enqueued && info->rel_time == 0) {
            user_sonos_request_seek_track(&enqueue_data->device, enqueue_data->num_enqueued,
                sonos_seek_callback, enqueue_data);
        }
        // On a previous track, likely paused
        else if(device_notify_time > 0 && (system_get_time() - device_notify_time < 600000000)
            && uuid_sid_match(enqueue_data->device.uuid, device_notify_info.subscribe_id)
            && info->rel_time > 0
            && (device_notify_info.transport_state == PAUSED_PLAYBACK)) {
            // Not sure of the best action here. We can do any of the following:
            // - Resume playback on the current track
            // - Skip to the next track
            // - Skip to the newly added track

            if (info->track < enqueue_data->num_enqueued) {
                // If the added track is greater than the paused track, skip ahead once
                user_sonos_request_seek_track(&enqueue_data->device, info->track + 1,
                    sonos_seek_callback, enqueue_data);
            }
            else {
                // Otherwise, just skip to the added track
                user_sonos_request_seek_track(&enqueue_data->device, enqueue_data->num_enqueued,
                    sonos_seek_callback, enqueue_data);
            }
        }
        // Likely currently playing, no need for more commands
        else {
            sonos_enqueue_cleanup(enqueue_data);
        }
    }
}

LOCAL void ICACHE_FLASH_ATTR sonos_set_transport_callback(void *user_data, bool success)
{
    sonos_enqueue_data *enqueue_data = (sonos_enqueue_data *)user_data;
    os_printf("sonos_set_transport_callback, success=%d\n", success);

    if (!success || !enqueue_data) {
        sonos_enqueue_cleanup(enqueue_data);
        return;
    }

    user_sonos_request_play(&enqueue_data->device, sonos_play_callback, enqueue_data);
}

LOCAL void ICACHE_FLASH_ATTR sonos_seek_callback(void *user_data, bool success)
{
    sonos_enqueue_data *enqueue_data = (sonos_enqueue_data *)user_data;
    os_printf("sonos_seek_callback, success=%d\n", success);

    if (!success || !enqueue_data) {
        sonos_enqueue_cleanup(enqueue_data);
        return;
    }

    user_sonos_request_play(&enqueue_data->device, sonos_play_callback, enqueue_data);
}

LOCAL void ICACHE_FLASH_ATTR sonos_play_callback(void *user_data, bool success)
{
    sonos_enqueue_data *enqueue_data = (sonos_enqueue_data *)user_data;
    os_printf("sonos_play_callback, success=%d\n", success);

    sonos_enqueue_cleanup(enqueue_data);
}

LOCAL void ICACHE_FLASH_ATTR sonos_enqueue_cleanup(sonos_enqueue_data *enqueue_data)
{
    if (enqueue_data) {
        os_free(enqueue_data);
    }
    enqueue_lock = false;    
}

LOCAL bool ICACHE_FLASH_ATTR uuid_sid_match(const char *uuid, const char *sid)
{
    int uuid_len = os_strlen(uuid);
    int sid_len = os_strlen(sid);
    return sid_len > uuid_len + 5
        && os_strncmp(uuid, sid + 5, uuid_len) == 0;
}