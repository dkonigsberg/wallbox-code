#include "user_sonos_listener.h"

#include <ets_sys.h>
#include <os_type.h>
#include <osapi.h>
#include <mem.h>
#include <user_interface.h>
#include <espconn.h>
#include <limits.h>
#include <sys/param.h>

#include "user_sonos_request.h"
#include "user_util.h"

#define LISTENER_PORT 3400
#define SUBSCRIBE_TIMEOUT_SECS 600
#define PACKET_SIZE 1024

typedef struct sonos_notification {
    char subscribe_id[64];
    char *payload_buf;
    int payload_len;
    int payload_max;
    bool has_header;
    bool error;
    os_timer_t disconnect_timer;
} sonos_notification;

LOCAL void ICACHE_FLASH_ATTR subscribe_request_callback(
    const sonos_subscribe_info *info, void *user_data, bool success);
LOCAL void ICACHE_FLASH_ATTR resubscribe_timer_callback(void *arg);
LOCAL void ICACHE_FLASH_ATTR sonos_listener_connect_callback(void *arg);
LOCAL void ICACHE_FLASH_ATTR sonos_listener_reconnect_callback(void *arg, sint8 err);
LOCAL void ICACHE_FLASH_ATTR sonos_listener_disconnect_callback(void *arg);
LOCAL void ICACHE_FLASH_ATTR sonos_listener_recv_callback(void *arg, char *pusrdata, unsigned short length);
LOCAL void ICACHE_FLASH_ATTR sonos_listener_disconnect_wait(void *arg);
LOCAL void ICACHE_FLASH_ATTR free_listener_connection(struct espconn *pespconn);

LOCAL esp_tcp sonos_listener_tcp;
LOCAL struct espconn sonos_listener_conn;
LOCAL user_sonos_listener_callback_t listener_callback = NULL;
LOCAL void *listener_callback_user_data = NULL;
LOCAL uint8 subscribe_request_lock = 0;
LOCAL sonos_device subscribed_device;
LOCAL sonos_subscribe_info subscribe_info;
LOCAL os_timer_t resubscribe_timer;

void ICACHE_FLASH_ATTR user_sonos_listener_init(void)
{
    os_memset(&subscribed_device, 0, sizeof(sonos_device));
    os_memset(&subscribe_info, 0, sizeof(sonos_subscribe_info));

    // Create a listener for Sonos notifications
    os_memset(&sonos_listener_tcp, 0, sizeof(esp_tcp));
    sonos_listener_tcp.local_port = LISTENER_PORT;

    os_memset(&sonos_listener_conn, 0, sizeof(struct espconn));
    sonos_listener_conn.type = ESPCONN_TCP;
    sonos_listener_conn.state = ESPCONN_NONE;
    sonos_listener_conn.proto.tcp = &sonos_listener_tcp;
    espconn_regist_connectcb(&sonos_listener_conn, sonos_listener_connect_callback);

    espconn_accept(&sonos_listener_conn);
}

void ICACHE_FLASH_ATTR user_sonos_listener_set_callback(user_sonos_listener_callback_t callback, void *user_data)
{
    listener_callback = callback;
    listener_callback_user_data = user_data;
}

void ICACHE_FLASH_ATTR user_sonos_listener_subscribe(const sonos_device *device)
{
    os_printf("user_sonos_listener_subscribe\n");

    if (subscribe_request_lock == 1) {
        os_printf("Subscription in progress\n");
        return;
    }

    struct ip_info ipconfig;
    if (!wifi_get_ip_info(STATION_IF, &ipconfig)) {
        os_printf("Could not get local IP info\n");
        return;
    }

    if (ipconfig.ip.addr == 0) {
        os_printf("Could not get valid local IP info\n");
        return;
    }

    uint8 local_ip[4];
    os_memcpy(local_ip, &ipconfig.ip, 4);

    if (!user_sonos_request_subscribe(device,
        local_ip, LISTENER_PORT, SUBSCRIBE_TIMEOUT_SECS,
        subscribe_request_callback, 0)) {
        return;
    }

    os_memcpy(&subscribed_device, device, sizeof(sonos_device));
    os_memset(&subscribe_info, 0, sizeof(sonos_subscribe_info));
    os_timer_disarm(&resubscribe_timer);
    subscribe_request_lock = 1;
}

LOCAL void ICACHE_FLASH_ATTR subscribe_request_callback(
    const sonos_subscribe_info *info, void *user_data, bool success)
{
    os_printf("subscribe_request_callback\n");

    if (info && success) {
        os_printf("Subscribed: sid=\"%s\", timeout=%d\n", info->subscribe_id, info->timeout_secs);

        os_memcpy(&subscribe_info, info, sizeof(sonos_subscribe_info));

        os_timer_disarm(&resubscribe_timer);
        if (info->timeout_secs > 0) {
            os_timer_setfn(&resubscribe_timer, (os_timer_func_t *)resubscribe_timer_callback, 0);
            os_timer_arm(&resubscribe_timer, (info->timeout_secs / 2) * 1000, 0);
        }
    }
    else {
        os_memset(&subscribed_device, 0, sizeof(sonos_device));
        os_memset(&subscribe_info, 0, sizeof(sonos_subscribe_info));
    }

    subscribe_request_lock = 0;
}

LOCAL void ICACHE_FLASH_ATTR resubscribe_timer_callback(void *arg)
{
    os_printf("resubscribe_timer_callback\n");

    os_timer_disarm(&resubscribe_timer);

    if (os_strlen(subscribe_info.subscribe_id) == 0) {
        os_printf("Aborting resubscribe due to lack of info\n");
        return;
    }

    struct ip_info ipconfig;
    if (!wifi_get_ip_info(STATION_IF, &ipconfig)) {
        os_printf("Could not get local IP info\n");
        return;
    }

    if (ipconfig.ip.addr == 0) {
        os_printf("Could not get valid local IP info\n");
        return;
    }

    uint8 local_ip[4];
    os_memcpy(local_ip, &ipconfig.ip, 4);

    if (!user_sonos_request_resubscribe(&subscribed_device,
        subscribe_info.subscribe_id, SUBSCRIBE_TIMEOUT_SECS,
        subscribe_request_callback, 0)) {
        return;
    }

    subscribe_request_lock = 1;
}

LOCAL void ICACHE_FLASH_ATTR sonos_listener_connect_callback(void *arg)
{
    struct espconn *pesp_conn = arg;

    os_printf("sonos_listener_connect_callback: %d.%d.%d.%d:%d\n",
        pesp_conn->proto.tcp->remote_ip[0],
        pesp_conn->proto.tcp->remote_ip[1],
        pesp_conn->proto.tcp->remote_ip[2],
        pesp_conn->proto.tcp->remote_ip[3],
        pesp_conn->proto.tcp->remote_port);

    sonos_notification *sn = (sonos_notification *)os_zalloc(sizeof(sonos_notification));
    if (!sn) {
        return;
    }
    pesp_conn->reverse = sn;

    espconn_regist_recvcb(pesp_conn, sonos_listener_recv_callback);
    espconn_regist_reconcb(pesp_conn, sonos_listener_reconnect_callback);
    espconn_regist_disconcb(pesp_conn, sonos_listener_disconnect_callback);
}

LOCAL void ICACHE_FLASH_ATTR sonos_listener_reconnect_callback(void *arg, sint8 err)
{
    struct espconn *pesp_conn = arg;
    sonos_notification *sn = (sonos_notification *)pesp_conn->reverse;

    os_printf("sonos_listener_reconnect_callback: %d.%d.%d.%d:%d, err=%d\n",
        pesp_conn->proto.tcp->remote_ip[0],
        pesp_conn->proto.tcp->remote_ip[1],
        pesp_conn->proto.tcp->remote_ip[2],
        pesp_conn->proto.tcp->remote_ip[3],
        pesp_conn->proto.tcp->remote_port,
        err);

    free_listener_connection(pesp_conn);
}

LOCAL void ICACHE_FLASH_ATTR sonos_listener_disconnect_callback(void *arg)
{
    struct espconn *pesp_conn = arg;
    sonos_notification *sn = (sonos_notification *)pesp_conn->reverse;

    os_printf("sonos_listener_disconnect_callback: %d.%d.%d.%d:%d\n",
        pesp_conn->proto.tcp->remote_ip[0],
        pesp_conn->proto.tcp->remote_ip[1],
        pesp_conn->proto.tcp->remote_ip[2],
        pesp_conn->proto.tcp->remote_ip[3],
        pesp_conn->proto.tcp->remote_port);
    
    if (!pesp_conn || !sn) {
        free_listener_connection(pesp_conn);
        return;
    }

    if (!sn->error && sn->has_header && sn->subscribe_id[0] != '\0' && sn->payload_len > 0) {
        LOCAL const char start_tags[] =
            "<e:propertyset xmlns:e=\"urn:schemas-upnp-org:event-1-0\"><e:property><LastChange>";
        LOCAL const char end_tags[] =
            "</LastChange></e:property></e:propertyset>";

        char *pstart = (char *)os_strstr(sn->payload_buf, start_tags);
        char *pend = (char *)os_strstr(sn->payload_buf, end_tags);
        if (pstart && pend && pstart < pend) {
            pstart += os_strlen(start_tags);
            int n = pend - pstart;

            unescape_html_entities(pstart, n);

            sonos_notify_info info;
            os_memcpy(info, 0, sizeof(sonos_notify_info));

            os_memcpy(info.subscribe_id, sn->subscribe_id, sizeof(info.subscribe_id));

            char *ptemp = NULL;
            char *qtemp = NULL;
            ptemp = (char *)os_strstr(pstart, "<TransportState val=\"");
            if (ptemp) {
                ptemp += 21;
                qtemp = (char *)os_strstr(ptemp, "\"/>");
                if (qtemp) {
                    n = qtemp - ptemp;
                    if (n == 7 && os_strncmp(ptemp, "PLAYING", 7) == 0) {
                        info.transport_state = PLAYING;
                    }
                    else if (n == 7 && os_strncmp(ptemp, "STOPPED", 7) == 0) {
                        info.transport_state = STOPPED;
                    }
                    else if (n == 15 && os_strncmp(ptemp, "PAUSED_PLAYBACK", 15) == 0) {
                        info.transport_state = PAUSED_PLAYBACK;
                    }
                    else {
                        os_printf("TransportState invalid\n");
                    }
                }
            }

            ptemp = (char *)os_strstr(pstart, "<NumberOfTracks val=\"");
            if (ptemp) {
                ptemp += 21;
                qtemp = (char *)os_strstr(ptemp, "\"/>");
                if (qtemp) {
                    long int num = strtol(ptemp, NULL, 10);
                    if (num >= 0 && num < INT_MAX) {
                        info.number_of_tracks = num;
                    } else {
                        os_printf("NumberOfTracks invalid: %d\n", num);
                    }
                }
            }

            ptemp = (char *)os_strstr(pstart, "<CurrentTrack val=\"");
            if (ptemp) {
                ptemp += 19;
                qtemp = (char *)os_strstr(ptemp, "\"/>");
                if (qtemp) {
                    long int num = strtol(ptemp, NULL, 10);
                    if (num >= 0 && num < INT_MAX) {
                        info.current_track = num;
                    } else {
                        os_printf("CurrentTrack invalid: %d\n", num);
                    }
                }
            }


            if (listener_callback) {
                listener_callback(&info, listener_callback_user_data);
            }
        }
        else {
            os_printf("Invalid event payload\n");
        }
    }

    free_listener_connection(pesp_conn);
}

LOCAL void ICACHE_FLASH_ATTR sonos_listener_recv_callback(void *arg, char *pusrdata, unsigned short length)
{
    struct espconn *pesp_conn = arg;
    sonos_notification *sn = (sonos_notification *)pesp_conn->reverse;

    //os_printf("sonos_listener_recv_callback: len=%d\n", length);

    if (!pusrdata || length == 0 || sn->error) {
        return;
    }

    if (!sn->has_header) {
        // Buffer and process the header section
        if (!sn->payload_buf) {
            sn->payload_buf = (char *)os_malloc(PACKET_SIZE * 2);
            if (!sn->payload_buf) {
                os_printf("Could not allocate header buffer\n");
                sn->error = true;
                return;
            }
            sn->payload_len = 0;
            sn->payload_max = PACKET_SIZE * 2;
        }

        if (length > sn->payload_max - sn->payload_len - 1) {
            os_printf("Header payload too large: n=%d\n", length);
            sn->error = true;
            return;
        }
        os_memcpy(sn->payload_buf + sn->payload_len, pusrdata, length);
        sn->payload_len += length;
        sn->payload_buf[sn->payload_len] = '\0';

        // Once we've buffered enough to have the content,
        // parse the header fields and move over.
        char *cstart = (char *)os_strstr(sn->payload_buf, "\r\n\r\n");
        if (cstart) {
            bool is_notify = false;
            bool is_event = false;
            bool is_propchange = false;
            int content_length = 0;
            char *ptemp = sn->payload_buf;
            char *qtemp = (char *)os_strstr(ptemp, "\r\n");
            while (ptemp && qtemp && (qtemp - ptemp > 0) && ptemp != cstart) {
                int n = qtemp - ptemp;
                if (ptemp == sn->payload_buf && n >= 23
                    && os_strncmp(ptemp, "NOTIFY /notify HTTP/1.1", 23) == 0) {
                    is_notify = true;
                }
                else if (n > 5 && os_strncmp(ptemp, "SID: ", 5) == 0) {
                    ptemp += 5; n -= 5;
                    if (n > sizeof(sn->subscribe_id) - 1) {
                        os_printf("SID too long: n=%d\n", n);
                        sn->error = true;
                        return;
                    }
                    os_memcpy(sn->subscribe_id, ptemp, n);
                    sn->subscribe_id[n] = '\0';
                }
                else if (n >= 14 && os_strncmp(ptemp, "NT: upnp:event", 14) == 0) {
                    is_event = true;
                }
                else if (n >= 20 && os_strncmp(ptemp, "NTS: upnp:propchange", 20) == 0) {
                    is_propchange = true;
                }
                else if (n > 16 && os_strncmp(ptemp, "CONTENT-LENGTH: ", 16) == 0) {
                    ptemp += 16; n -= 16;
                    long int len_value = strtol(ptemp, NULL, 10);
                    if (len_value <= 0 || len_value > INT_MAX) {
                        os_printf("Length invalid: len=%d\n", len_value);
                        sn->error = true;
                        return;
                    }
                    content_length = len_value;
                }

                qtemp += 2;
                ptemp = qtemp;
                qtemp = (char *)os_strstr(ptemp, "\r\n");
            }

            if (is_notify && is_event && is_propchange
                && sn->subscribe_id[0] != '\0' && content_length > 0) {

                os_printf("Notify message from: \"%s\"\n", sn->subscribe_id);
                
                if (os_strcmp(sn->subscribe_id, subscribe_info.subscribe_id) != 0) {
                    os_printf("Notification SID does not match active subscription\n");
                    sn->error = true;
                    return;
                }

                char *content_buf = (char *)os_malloc(content_length + 1);
                if (!content_buf) {
                    os_printf("Could not allocate content buffer\n");
                    sn->error = true;
                    return;
                }

                // Advance the content start pointer past the delimiter
                cstart += 4;
                int n = sn->payload_len - (cstart - sn->payload_buf);
                if (n < 0 || n > content_length) {
                    os_printf("Invalid copy len: %d\n", n);
                    os_free(content_buf);
                    sn->error = true;
                    return;
                }

                os_memcpy(content_buf, cstart, n);
                content_buf[n] = '\0';

                // Replace the saved buffer with the new content buffer
                os_free(sn->payload_buf);
                sn->payload_buf = content_buf;
                sn->payload_len = n;
                sn->payload_max = content_length;

                sn->has_header = true;
            }
            else {
                os_printf("Not a valid notify header\n");
                sn->error = true;
                return;
            }
        }
    }
    else {
        // Buffer the content section
        if (length > sn->payload_max - sn->payload_len) {
            os_printf("Content payload too large: n=%d\n", length);
            sn->error = true;
            return;
        }

        os_memcpy(sn->payload_buf + sn->payload_len, pusrdata, length);
        sn->payload_len += length;
        sn->payload_buf[sn->payload_len] = '\0';

        if (sn->payload_len == sn->payload_max) {
            os_timer_disarm(&sn->disconnect_timer);
            os_timer_setfn(&sn->disconnect_timer, (os_timer_func_t *)sonos_listener_disconnect_wait, pesp_conn);
            os_timer_arm(&sn->disconnect_timer, 10, 0);
        }
    }
}

LOCAL void ICACHE_FLASH_ATTR sonos_listener_disconnect_wait(void *arg)
{
    //os_printf("sonos_listener_disconnect_wait\n");
    struct espconn *pespconn = (struct espconn *)arg;

    if(pespconn) {
        sonos_notification *sn = (sonos_notification *)pespconn->reverse;
        os_timer_disarm(&sn->disconnect_timer);
        espconn_disconnect(pespconn);
    }
}

LOCAL void ICACHE_FLASH_ATTR free_listener_connection(struct espconn *pespconn)
{
    if (pespconn) {
        sonos_notification *sn = (sonos_notification *)pespconn->reverse;

        if (sn) {
            os_timer_disarm(&sn->disconnect_timer);

            if (sn->payload_buf) {
                os_free(sn->payload_buf);
            }
            os_free(sn);
            pespconn->reverse = NULL;
        }
    }
}