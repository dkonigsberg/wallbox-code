#include "user_sonos_request.h"

#include <ets_sys.h>
#include <os_type.h>
#include <osapi.h>
#include <mem.h>
#include <user_interface.h>
#include <espconn.h>
#include <limits.h>
#include <sys/param.h>
#include <stdlib.h>

#include "user_util.h"

#define PACKET_SIZE (2 * 1024)

typedef enum sonos_request_type {
    REQUEST_ADD_URI = 0,
    REQUEST_SET_TRANSPORT,
    REQUEST_SEEK,
    REQUEST_PLAY,
    REQUEST_GET_POSITION_INFO,
    REQUEST_SUBSCRIBE,
    REQUEST_RESUBSCRIBE
} sonos_request_type;

typedef struct sonos_request {
    sonos_device device;
    char *payload;
    int payload_len;
    os_timer_t disconnect_timer;
    sonos_request_type request_type;
    char *response_buf;
    size_t response_len;
    void *callback;
    void *user_data;
    bool result_notified;
} sonos_request;

LOCAL sonos_request* ICACHE_FLASH_ATTR sonos_build_request(const sonos_device *device, const char *action, const char *content);
LOCAL void ICACHE_FLASH_ATTR sonos_request_start(sonos_request *request);
LOCAL void ICACHE_FLASH_ATTR sonos_request_connect_callback(void *arg);
LOCAL void ICACHE_FLASH_ATTR sonos_request_disconnect_callback(void *arg);
LOCAL void ICACHE_FLASH_ATTR sonos_request_reconnect_callback(void *arg, sint8 err);
LOCAL void ICACHE_FLASH_ATTR sonos_request_sent_callback(void *arg);
LOCAL void ICACHE_FLASH_ATTR sonos_request_recv_callback(void *arg, char *pusrdata, unsigned short length);
LOCAL void ICACHE_FLASH_ATTR sonos_request_disconnect_wait(void *arg);
LOCAL void ICACHE_FLASH_ATTR notify_request_listener(sonos_request *request, bool is_success);
LOCAL void ICACHE_FLASH_ATTR free_tcp_connection(struct espconn *pespconn);

void ICACHE_FLASH_ATTR user_sonos_request_init(void)
{
}

bool ICACHE_FLASH_ATTR user_sonos_request_add_uri(const sonos_device *device, const char *uri,
    user_sonos_request_add_uri_callback_t callback, void *user_data)
{
    LOCAL const char action[] = "urn:schemas-upnp-org:service:AVTransport:1#AddURIToQueue";

    char *content_buf = (char *)os_malloc(PACKET_SIZE);
    if (!content_buf) {
        return false;
    }

    //Note: Assuming that URI is already URL-encoded
    os_sprintf(content_buf,
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                    "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
          "<s:Body>"
            "<u:AddURIToQueue xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
              "<InstanceID>0</InstanceID>"
              "<EnqueuedURI>%s</EnqueuedURI>"
              "<EnqueuedURIMetaData></EnqueuedURIMetaData>"
              "<DesiredFirstTrackNumberEnqueued>0</DesiredFirstTrackNumberEnqueued>"
              "<EnqueueAsNext>0</EnqueueAsNext>"
            "</u:AddURIToQueue>"
          "</s:Body>"
        "</s:Envelope>", uri);

    sonos_request *request = sonos_build_request(device, action, content_buf);
    os_free(content_buf);
    if (!request) {
        return false;
    }
    request->request_type = REQUEST_ADD_URI;
    request->callback = callback;
    request->user_data = user_data;

    sonos_request_start(request);
    return true;
}

bool ICACHE_FLASH_ATTR user_sonos_request_set_transport(const sonos_device *device,
    user_sonos_request_callback_t callback, void *user_data)
{
    LOCAL const char action[] = "urn:schemas-upnp-org:service:AVTransport:1#SetAVTransportURI";

    char *content_buf = (char *)os_malloc(PACKET_SIZE);
    if (!content_buf) {
        return false;
    }

    os_sprintf(content_buf,
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                    "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
          "<s:Body>"
            "<u:SetAVTransportURI xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
              "<InstanceID>0</InstanceID>"
              "<CurrentURI>x-rincon-queue:%s#0</CurrentURI>"
              "<CurrentURIMetaData></CurrentURIMetaData>"
            "</u:SetAVTransportURI>"
          "</s:Body>"
        "</s:Envelope>",
        device->uuid);

    sonos_request *request = sonos_build_request(device, action, content_buf);
    os_free(content_buf);
    if (!request) {
        return false;
    }
    request->request_type = REQUEST_SET_TRANSPORT;
    request->callback = callback;
    request->user_data = user_data;

    sonos_request_start(request);
    return true;
}

bool ICACHE_FLASH_ATTR user_sonos_request_seek_track(const sonos_device *device, int track,
    user_sonos_request_callback_t callback, void *user_data)
{
    LOCAL const char action[] = "urn:schemas-upnp-org:service:AVTransport:1#Seek";

    char *content_buf = (char *)os_malloc(PACKET_SIZE);
    if (!content_buf) {
        return false;
    }

    os_sprintf(content_buf,
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                    "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
          "<s:Body>"
            "<u:Seek xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
              "<InstanceID>0</InstanceID>"
              "<Unit>TRACK_NR</Unit>"
              "<Target>%d</Target>"
            "</u:Seek>"
          "</s:Body>"
        "</s:Envelope>",
        track);

    sonos_request *request = sonos_build_request(device, action, content_buf);
    os_free(content_buf);
    if (!request) {
        return false;
    }
    request->request_type = REQUEST_SEEK;
    request->callback = callback;
    request->user_data = user_data;

    sonos_request_start(request);
    return true;
}

bool ICACHE_FLASH_ATTR user_sonos_request_play(const sonos_device *device,
    user_sonos_request_callback_t callback, void *user_data)
{
    LOCAL const char action[] = "urn:schemas-upnp-org:service:AVTransport:1#Play";

    LOCAL const char content[] =
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                    "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
          "<s:Body>"
            "<u:Play xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
              "<InstanceID>0</InstanceID>"
              "<Speed>1</Speed>"
            "</u:Play>"
          "</s:Body>"
        "</s:Envelope>";

    sonos_request *request = sonos_build_request(device, action, content);
    if (!request) {
        return false;
    }
    request->request_type = REQUEST_PLAY;
    request->callback = callback;
    request->user_data = user_data;

    sonos_request_start(request);
    return true;
}

bool ICACHE_FLASH_ATTR user_sonos_request_get_position_info(const sonos_device *device,
    user_sonos_request_position_callback_t callback, void *user_data)
{
    LOCAL const char action[] = "urn:schemas-upnp-org:service:AVTransport:1#GetPositionInfo";

    LOCAL const char content[] =
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                    "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
          "<s:Body>"
            "<u:GetPositionInfo xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
              "<InstanceID>0</InstanceID>"
            "</u:GetPositionInfo>"
          "</s:Body>"
        "</s:Envelope>";

    sonos_request *request = sonos_build_request(device, action, content);
    if (!request) {
        return false;
    }
    request->request_type = REQUEST_GET_POSITION_INFO;
    request->callback = callback;
    request->user_data = user_data;

    sonos_request_start(request);
    return true;
}

LOCAL sonos_request* ICACHE_FLASH_ATTR sonos_build_request(const sonos_device *device, const char *action, const char *content)
{
    sonos_request *request = (sonos_request *)os_zalloc(sizeof(sonos_request));
    if (!request) {
        return NULL;
    }

    os_memcpy(&request->device, device, sizeof(sonos_device));

    int content_len = os_strlen(content);

    char *pbuf = (char *)os_malloc(PACKET_SIZE);
    if (!pbuf) {
        os_free(request);
        return NULL;
    }

    // Play:              /MediaRenderer/AVTransport/Control
    // AddURIToQueue:     /MediaRenderer/AVTransport/Control
    // SetAVTransportURI: /MediaRenderer/AVTransport/Control
    // RemoveAllTracks:   /MediaRenderer/Queue/Control

    os_sprintf(pbuf,
        "POST /MediaRenderer/AVTransport/Control HTTP/1.1\r\n"
        "Host: " IPSTR ":%d\r\n"
        "Connection: close\r\n"
        "User-Agent: lwIP/1.4.0\r\n"
        "Content-Type: text/xml; charset=\"utf-8\"\r\n"
        "Content-Length: %d\r\n"
        "X-SONOS-TARGET-UDN: uuid:%s\r\n"
        "SOAPACTION: \"%s\"\r\n"
        "\r\n%s",
        IP2STR(device->ip), device->port,
        content_len, device->uuid,
        action, content);

    request->payload_len = os_strlen(pbuf);
    request->payload = (char *)os_malloc(request->payload_len + 1);
    if(!request->payload) {
        os_free(pbuf);
        os_free(request);
        return NULL;
    }

    os_strcpy(request->payload, pbuf);
    os_free(pbuf);
    return request;
}

bool user_sonos_request_subscribe(const sonos_device *device,
    uint8 listener_ip[4], int listener_port, int timeout_secs,
    user_sonos_request_subscribe_callback_t callback, void *user_data)
{
    sonos_request *request = (sonos_request *)os_zalloc(sizeof(sonos_request));
    if (!request) {
        return false;
    }

    os_memcpy(&request->device, device, sizeof(sonos_device));

    char *pbuf = (char *)os_malloc(PACKET_SIZE);
    if (!pbuf) {
        os_free(request);
        return false;
    }

    os_sprintf(pbuf,
        "SUBSCRIBE /MediaRenderer/AVTransport/Event HTTP/1.1\r\n"
        "Host: " IPSTR ":%d\r\n"
        "User-Agent: lwIP/1.4.0\r\n"
        "CALLBACK: <http://" IPSTR ":%d/notify>\r\n"
        "NT: upnp:event\r\n"
        "TIMEOUT: Second-%d\r\n"
        "Connection: close\r\n"
        "Content-Length: 0\r\n"
        "\r\n",
        IP2STR(request->device.ip), request->device.port,
        IP2STR(listener_ip), listener_port,
        timeout_secs);

    request->payload_len = os_strlen(pbuf);
    request->payload = (char *)os_malloc(request->payload_len + 1);
    if(!request->payload) {
        os_free(pbuf);
        os_free(request);
        return false;
    }

    os_strcpy(request->payload, pbuf);
    os_free(pbuf);

    request->request_type = REQUEST_SUBSCRIBE;
    request->callback = callback;
    request->user_data = user_data;

    sonos_request_start(request);
    return true;
}

bool user_sonos_request_resubscribe(const sonos_device *device,
    const char *subscribe_id, int timeout_secs,
    user_sonos_request_subscribe_callback_t callback, void *user_data)
{
    sonos_request *request = (sonos_request *)os_zalloc(sizeof(sonos_request));
    if (!request) {
        return false;
    }

    os_memcpy(&request->device, device, sizeof(sonos_device));

    char *pbuf = (char *)os_malloc(PACKET_SIZE);
    if (!pbuf) {
        os_free(request);
        return false;
    }

    os_sprintf(pbuf,
        "SUBSCRIBE /MediaRenderer/AVTransport/Event HTTP/1.1\r\n"
        "Host: " IPSTR ":%d\r\n"
        "SID: %s\r\n"
        "TIMEOUT: Second-%d\r\n"
        "Connection: close\r\n"
        "Content-Length: 0\r\n"
        "\r\n",
        IP2STR(request->device.ip), request->device.port,
        subscribe_id, timeout_secs);

    request->payload_len = os_strlen(pbuf);
    request->payload = (char *)os_malloc(request->payload_len + 1);
    if(!request->payload) {
        os_free(pbuf);
        os_free(request);
        return false;
    }

    os_strcpy(request->payload, pbuf);
    os_free(pbuf);

    request->request_type = REQUEST_RESUBSCRIBE;
    request->callback = callback;
    request->user_data = user_data;
    
    sonos_request_start(request);
    return true;
}

LOCAL void ICACHE_FLASH_ATTR sonos_request_start(sonos_request *request)
{
    struct espconn *pespconn = (struct espconn *)os_zalloc(sizeof(struct espconn));
    pespconn->type = ESPCONN_TCP;
    pespconn->state = ESPCONN_NONE;
    pespconn->proto.tcp = (esp_tcp *)os_zalloc(sizeof(esp_tcp));
    pespconn->proto.tcp->local_port = espconn_port();
    pespconn->proto.tcp->remote_port = request->device.port;
    os_memcpy(pespconn->proto.tcp->remote_ip, request->device.ip, 4);
    espconn_regist_connectcb(pespconn, sonos_request_connect_callback);
    espconn_regist_disconcb(pespconn, sonos_request_disconnect_callback);
    espconn_regist_reconcb(pespconn, sonos_request_reconnect_callback);
    pespconn->reverse = request;
    espconn_connect(pespconn);
}

LOCAL void ICACHE_FLASH_ATTR sonos_request_connect_callback(void *arg)
{
    int result = 0;
    struct espconn *pespconn = (struct espconn *)arg;
    sonos_request *request = (sonos_request *)pespconn->reverse;

    //os_printf("sonos_request_connect_callback\n");

    espconn_regist_sentcb(pespconn, sonos_request_sent_callback);
    espconn_regist_recvcb(pespconn, sonos_request_recv_callback);

    result = espconn_sent(pespconn, (uint8 *)request->payload, request->payload_len);
    if (result != ESPCONN_OK) {
        os_printf("espconn_sent error: %d\n", result);
    }
}

LOCAL void ICACHE_FLASH_ATTR sonos_request_disconnect_callback(void *arg)
{
    struct espconn *pespconn = (struct espconn *)arg;
    sonos_request *request = (sonos_request *)pespconn->reverse;

    //os_printf("sonos_request_disconnect_callback\n");

    int response_code = 0;
    bool is_success = false;
    char *hstart = NULL;
    char *pstart = NULL;

    if (request && request->response_len > 9 && os_strncmp(request->response_buf, "HTTP/1.1 ", 9) == 0) {
        long int code = strtol(request->response_buf + 9, NULL, 10);
        if(code >= 0 && code <= 999) {
            response_code = code;
        }
        hstart = (char *)os_strstr(request->response_buf + 9, "\r\n");
        if (hstart) {
            hstart += 2;
            pstart = (char *)os_strstr(hstart, "\r\n\r\n");
            if (pstart) {
                pstart += 4;
            }
        }
    }

    is_success = (response_code == 200);

    os_printf("Request complete, code=%d\n", response_code);

    if (is_success && pstart && request->request_type == REQUEST_ADD_URI) {
        char *ptemp = NULL;
        char *qtemp = NULL;
        char buf[128];

        sonos_add_uri_info info;
        os_bzero(&info, sizeof(sonos_add_uri_info));

        ptemp = (char *)os_strstr(pstart, "<FirstTrackNumberEnqueued>");
        if (ptemp) {
            ptemp += 26;
            qtemp = (char *)os_strstr(ptemp, "</FirstTrackNumberEnqueued>");
            if (qtemp) {
                int n = MIN(qtemp - ptemp, 127);
                os_memcpy(buf, ptemp, n);
                buf[n] = '\0';
                long int num = strtol(buf, NULL, 10);
                if (num >= 0 && num < INT_MAX) {
                    info.first_track_num_enqueued = num;
                }
            }
        }

        ptemp = (char *)os_strstr(pstart, "<NumTracksAdded>");
        if (ptemp) {
            ptemp += 16;
            qtemp = (char *)os_strstr(ptemp, "</NumTracksAdded>");
            if (qtemp) {
                int n = MIN(qtemp - ptemp, 127);
                os_memcpy(buf, ptemp, n);
                buf[n] = '\0';
                long int num = strtol(buf, NULL, 10);
                if (num >= 0 && num < INT_MAX) {
                    info.num_tracks_added = num;
                }
            }
        }

        ptemp = (char *)os_strstr(pstart, "<NewQueueLength>");
        if (ptemp) {
            ptemp += 16;
            qtemp = (char *)os_strstr(ptemp, "</NewQueueLength>");
            if (qtemp) {
                int n = MIN(qtemp - ptemp, 127);
                os_memcpy(buf, ptemp, n);
                buf[n] = '\0';
                long int num = strtol(buf, NULL, 10);
                if (num >= 0 && num < INT_MAX) {
                    info.new_queue_length = num;
                }
            }
        }

        ((user_sonos_request_add_uri_callback_t)request->callback)(
            &info, request->user_data, is_success);
        request->result_notified = true;
    }
    else if (is_success && pstart && request->request_type == REQUEST_GET_POSITION_INFO) {
        char *ptemp = NULL;
        char *qtemp = NULL;
        char buf[128];

        sonos_position_info info;
        os_bzero(&info, sizeof(sonos_position_info));

        ptemp = (char *)os_strstr(pstart, "<Track>");
        if (ptemp) {
            ptemp += 7;
            qtemp = (char *)os_strstr(ptemp, "</Track>");
            if (qtemp) {
                int n = MIN(qtemp - ptemp, 127);
                os_memcpy(buf, ptemp, n);
                buf[n] = '\0';
                long int track_value = strtol(buf, NULL, 10);
                if (track_value >= 0 && track_value < INT_MAX) {
                    info.track = track_value;
                }
            }
        }

        ptemp = (char *)os_strstr(pstart, "<TrackDuration>");
        if (ptemp) {
            ptemp += 15;
            qtemp = (char *)os_strstr(ptemp, "</TrackDuration>");
            if (qtemp) {
                int n = MIN(qtemp - ptemp, 127);
                os_memcpy(buf, ptemp, n);
                buf[n] = '\0';
                int duration_value = str_to_seconds(buf);
                if (duration_value >= 0) {
                    info.track_duration = duration_value;
                }
            }
        }

        ptemp = (char *)os_strstr(pstart, "<TrackURI>");
        if (ptemp) {
            ptemp += 10;
            qtemp = (char *)os_strstr(ptemp, "</TrackURI>");
            if (qtemp) {
                int n = MIN(qtemp - ptemp, sizeof(info.track_uri) - 1);
                os_memcpy(info.track_uri, ptemp, n);
                info.track_uri[n] = '\0';
            }
        }

        ptemp = (char *)os_strstr(pstart, "<RelTime>");
        if (ptemp) {
            ptemp += 9;
            qtemp = (char *)os_strstr(ptemp, "</RelTime>");
            if (qtemp) {
                int n = MIN(qtemp - ptemp, 127);
                os_memcpy(buf, ptemp, n);
                buf[n] = '\0';
                int rel_value = str_to_seconds(buf);
                if (rel_value >= 0) {
                    info.rel_time = rel_value;
                }
            }
        }

        ((user_sonos_request_position_callback_t)request->callback)(
            &info, request->user_data, is_success);
        request->result_notified = true;
    }
    else if (is_success && hstart && (request->request_type == REQUEST_SUBSCRIBE
            || request->request_type == REQUEST_RESUBSCRIBE)) {
        sonos_subscribe_info info;
        os_bzero(&info, sizeof(sonos_subscribe_info));

        char *ptemp = hstart;
        char *qtemp = (char *)os_strstr(ptemp, "\r\n");
        while (ptemp && qtemp && (qtemp - ptemp > 0) && !(pstart && ptemp == pstart)) {
            int n = qtemp - ptemp;
            if (n > 5 && os_strncmp(ptemp, "SID: ", 5) == 0) {
                ptemp += 5; n -= 5;
                if (n > sizeof(info.subscribe_id) - 1) {
                    os_printf("SID too long: n=%d\n", n);
                    break;
                }
                os_memcpy(info.subscribe_id, ptemp, n);
                info.subscribe_id[n] = '\0';
            }
            else if (n > 16 && os_strncmp(ptemp, "TIMEOUT: Second-", 16) == 0) {
                ptemp += 16; n -= 16;
                long int sec_value = strtol(ptemp, NULL, 10);
                if (sec_value <= 0 || sec_value > INT_MAX) {
                    os_printf("Timeout invalid: t=%ld\n", sec_value);
                    break;
                }
                info.timeout_secs = sec_value;
            }

            qtemp += 2;
            ptemp = qtemp;
            qtemp = (char *)os_strstr(ptemp, "\r\n");
        }

        ((user_sonos_request_subscribe_callback_t)request->callback)(
            &info, request->user_data, is_success);
        request->result_notified = true;
    }
    else {
        notify_request_listener(request, is_success);
    }

    free_tcp_connection(pespconn);
}

LOCAL void ICACHE_FLASH_ATTR sonos_request_reconnect_callback(void *arg, sint8 err)
{
    struct espconn *pespconn = (struct espconn *)arg;
    os_printf("sonos_request_reconnect_callback, err=%d\n", err);
    free_tcp_connection(pespconn);
}

LOCAL void ICACHE_FLASH_ATTR sonos_request_sent_callback(void *arg)
{
    struct espconn *pespconn = (struct espconn *)arg;
    sonos_request *request = (sonos_request *)pespconn->reverse;

    os_printf("sonos_request_sent_callback\n");

    if (request->payload) {
        os_free(request->payload);
        request->payload = NULL;
    }
    request->payload_len = 0;
}

LOCAL void ICACHE_FLASH_ATTR sonos_request_recv_callback(void *arg, char *pusrdata, unsigned short length)
{
    struct espconn *pespconn = (struct espconn *)arg;
    sonos_request *request = (sonos_request *)pespconn->reverse;

    //os_printf("sonos_request_recv_callback, state=%d\n", pespconn->state);

    if (pusrdata && length > 0) {
        if (!request->response_buf) {
            request->response_buf = (char *)os_malloc(PACKET_SIZE);
            request->response_len = 0;
        }
        int n = MIN(length, PACKET_SIZE - request->response_len - 1);
        if (n > 0) {
            os_memcpy(request->response_buf + request->response_len, pusrdata, n);
            request->response_len += n;
            request->response_buf[request->response_len] = '\0';
        }
    }

    if (request->response_len >= PACKET_SIZE - 1
        || os_strstr(request->response_buf, "</s:Envelope>")) {
        // Use a short timer delay for the actual disconnect call,
        // per recommendation from the docs.
        os_timer_disarm(&request->disconnect_timer);
        os_timer_setfn(&request->disconnect_timer, (os_timer_func_t *)sonos_request_disconnect_wait, pespconn);
        os_timer_arm(&request->disconnect_timer, 10, 0);
    }
}

LOCAL void ICACHE_FLASH_ATTR sonos_request_disconnect_wait(void *arg)
{
    //os_printf("sonos_request_disconnect_wait\n");
    struct espconn *pespconn = (struct espconn *)arg;

    if(pespconn) {
        sonos_request *request = (sonos_request *)pespconn->reverse;
        os_timer_disarm(&request->disconnect_timer);
        espconn_disconnect(pespconn);
    }
}

LOCAL void ICACHE_FLASH_ATTR notify_request_listener(sonos_request *request, bool is_success)
{
    if (request && !request->result_notified && request->callback) {
        switch(request->request_type) {
        case REQUEST_SET_TRANSPORT:
        case REQUEST_SEEK:
        case REQUEST_PLAY:
            ((user_sonos_request_callback_t)request->callback)(
                request->user_data, is_success);
            break;
        case REQUEST_ADD_URI:
            ((user_sonos_request_add_uri_callback_t)request->callback)(
                NULL, request->user_data, is_success);
            break;
        case REQUEST_GET_POSITION_INFO:
            ((user_sonos_request_position_callback_t)request->callback)(
                NULL, request->user_data, is_success);
            break;
        case REQUEST_SUBSCRIBE:
        case REQUEST_RESUBSCRIBE:
            ((user_sonos_request_subscribe_callback_t)request->callback)(
                NULL, request->user_data, is_success);
            break;
        default:
            break;
        }
    }
    if (request) {
        request->result_notified = true;
    }
}

LOCAL void ICACHE_FLASH_ATTR free_tcp_connection(struct espconn *pespconn)
{
    //os_printf("free_tcp_connection\n");
    if (pespconn) {
        sonos_request *request = (sonos_request *)pespconn->reverse;

        os_timer_disarm(&request->disconnect_timer);

        if (request->payload) {
            os_free(request->payload);
        }

        if (request->response_buf) {
            os_free(request->response_buf);
            request->response_buf = NULL;
        }
        request->response_len = 0;

        notify_request_listener(request, false);

        os_free(request);
        pespconn->reverse = NULL;

        if (pespconn->proto.tcp) {
            os_free(pespconn->proto.tcp);
        }
        os_free(pespconn);
    }
}
