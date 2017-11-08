#include "user_sonos_discovery.h"

#include <ets_sys.h>
#include <os_type.h>
#include <osapi.h>
#include <mem.h>
#include <user_interface.h>
#include <espconn.h>
#include <sys/queue.h>
#include <limits.h>
#include <sys/param.h>
#include <stdlib.h>

#include "user_util.h"

#define SSDP_PORT 1900
#define LOCAL_PORT 53000
#define SSDP_REQUEST_TIMEOUT 5000
#define PACKET_SIZE (2 * 1024)

struct sonos_device_node {
    sonos_device device;
    bool zp_request_sent;
    char *zp_response_buf;
    size_t zp_response_len;
    SLIST_ENTRY(sonos_device_node) next;
};

LOCAL void ICACHE_FLASH_ATTR ssdp_recv_callback(void *arg, char *pusrdata, unsigned short length);
LOCAL void ICACHE_FLASH_ATTR ssdp_request_timer_callback(void);
LOCAL void ICACHE_FLASH_ATTR zp_request_connect_callback(void *arg);
LOCAL void ICACHE_FLASH_ATTR zp_request_disconnect_callback(void *arg);
LOCAL void ICACHE_FLASH_ATTR zp_request_reconnect_callback(void *arg, sint8 err);
LOCAL void ICACHE_FLASH_ATTR zp_request_sent_callback(void *arg);
LOCAL void ICACHE_FLASH_ATTR zp_request_recv_callback(void *arg, char *pusrdata, unsigned short length);
LOCAL void ICACHE_FLASH_ATTR free_tcp_connection(struct espconn *pespconn);

LOCAL esp_udp ssdp_listener_udp;
LOCAL struct espconn ssdp_listener_conn;
LOCAL esp_udp ssdp_request_udp;
LOCAL struct espconn ssdp_request_conn;
LOCAL uint8 ssdp_request_lock = 0;
LOCAL os_timer_t ssdp_request_timer;
LOCAL user_sonos_discovery_callback_t discovery_callback = NULL;
LOCAL void *discovery_callback_user_data = NULL;

LOCAL SLIST_HEAD(, sonos_device_node) sonos_device_list = SLIST_HEAD_INITIALIZER(sonos_device_list);

void ICACHE_FLASH_ATTR user_sonos_discovery_init(void)
{
    SLIST_INIT(&sonos_device_list);

    // Create a listener for SSDP broadcasts
    os_bzero(&ssdp_listener_udp, sizeof(esp_udp));
    ssdp_listener_udp.local_port = SSDP_PORT;

    os_bzero(&ssdp_listener_conn, sizeof(struct espconn));
    ssdp_listener_conn.type = ESPCONN_UDP;
    ssdp_listener_conn.proto.udp = &ssdp_listener_udp;
    espconn_regist_recvcb(&ssdp_listener_conn, ssdp_recv_callback);
    espconn_create(&ssdp_listener_conn);
}

void ICACHE_FLASH_ATTR user_sonos_discovery_start(void)
{
    os_printf("Discovering devices...\n");

    // Start the explicit discovery process
    int result = 0;

    LOCAL const char DISCOVERY_PAYLOAD[] =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:reservedSSDPport\r\n"
        "MAN: ssdp:discover\r\n"
        "MX: 1\r\n"
        "ST: urn:schemas-upnp-org:device:ZonePlayer:1\r\n";

    if (ssdp_request_lock == 1) {
        os_printf("Discovery in progress\n");
        return;
    }

    os_bzero(&ssdp_request_udp, sizeof(esp_udp));
    ssdp_request_udp.local_port = LOCAL_PORT;
    ssdp_request_udp.remote_ip[0] = 239;
    ssdp_request_udp.remote_ip[1] = 255;
    ssdp_request_udp.remote_ip[2] = 255;
    ssdp_request_udp.remote_ip[3] = 250;
    ssdp_request_udp.remote_port = SSDP_PORT;

    os_bzero(&ssdp_request_conn, sizeof(struct espconn));
    ssdp_request_conn.type = ESPCONN_UDP;
    ssdp_request_conn.proto.udp = &ssdp_request_udp;

    result = espconn_regist_recvcb(&ssdp_request_conn, ssdp_recv_callback);
    if (result != 0) {
        os_printf("espconn_regist_recvcb error: %d\n", result);
        return;
    }

    result = espconn_create(&ssdp_request_conn);
    if (result != 0) {
        os_printf("espconn_create error: %d\n", result);
        return;
    }

    result = espconn_sendto(&ssdp_request_conn, (uint8 *)DISCOVERY_PAYLOAD, sizeof(DISCOVERY_PAYLOAD) - 1);
    if (result != 0) {
        os_printf("espconn_sendto error: %d\n", result);
        espconn_delete(&ssdp_request_conn);
        return;
    }

    os_timer_disarm(&ssdp_request_timer);
    os_timer_setfn(&ssdp_request_timer, (os_timer_func_t *)ssdp_request_timer_callback, NULL);
    os_timer_arm(&ssdp_request_timer, SSDP_REQUEST_TIMEOUT, 0);
    ssdp_request_lock = 1;
}

void ICACHE_FLASH_ATTR user_sonos_discovery_abort()
{
    if (ssdp_request_lock == 1) {
        os_timer_disarm(&ssdp_request_timer);
        espconn_delete(&ssdp_request_conn);
        ssdp_request_lock = 0;
        os_printf("Discovery aborted\n");
    }
}

void ICACHE_FLASH_ATTR user_sonos_discovery_set_callback(
    user_sonos_discovery_callback_t callback, void *user_data)
{
    discovery_callback = callback;
    discovery_callback_user_data = user_data;
}

int ICACHE_FLASH_ATTR user_sonos_discovery_get_device_by_uuid(sonos_device *device, const char *uuid)
{
    if (!device || !uuid) {
        return -1;
    }

    struct sonos_device_node *np;
    SLIST_FOREACH(np, &sonos_device_list, next) {
        if (os_strcmp(np->device.uuid, uuid) == 0) {
            os_memcpy(device, &np->device, sizeof(sonos_device));
            return 1;
        }
    }
    return 0;
}

int ICACHE_FLASH_ATTR user_sonos_discovery_get_device_by_name(sonos_device *device, const char *zone_name)
{
    if (!device || !zone_name) {
        return -1;
    }

    struct sonos_device_node *np;
    SLIST_FOREACH(np, &sonos_device_list, next) {
        if (os_strcmp(np->device.zone_name, zone_name) == 0) {
            os_memcpy(device, &np->device, sizeof(sonos_device));
            return 1;
        }
    }
    return 0;
}

bool ICACHE_FLASH_ATTR user_sonos_discovery_json_devices(char **json_data)
{
    char buf[512];
    int buf_len = 0;
    char *out_buf;
    int out_len = 0;
    int out_max = 512;

    out_buf = (char *)os_malloc(out_max);
    if (!out_buf) {
        return false;
    }

    out_buf[0] = '[';
    out_len++;

    struct sonos_device_node *np;
    SLIST_FOREACH(np, &sonos_device_list, next) {
        os_sprintf(buf,
            "{"
            "\"ip\": \"" IPSTR "\", "
            "\"port\": \"%d\", "
            "\"uuid\": \"%s\", "
            "\"zone_name\": \"%s\""
            "}%s",
            IP2STR(np->device.ip), np->device.port,
            np->device.uuid, np->device.zone_name,
            SLIST_NEXT(np, next) ? ", " : "]");
        buf_len = os_strlen(buf);

        if (out_len + buf_len >= out_max - 1) {
            out_max += buf_len;
            char *temp = (char *)os_malloc(out_max);
            if (!temp) {
                os_free(out_buf);
                return false;
            }
            os_memcpy(temp, out_buf, out_len);
            os_free(out_buf);
            out_buf = temp;
        }

        os_memcpy(out_buf + out_len, buf, buf_len + 1);
        out_len += buf_len;
    }

    *json_data = out_buf;

    return true;
}

LOCAL void ICACHE_FLASH_ATTR ssdp_recv_callback(void *arg, char *pusrdata, unsigned short length)
{
    int result = 0;
    struct espconn *pesp_conn = arg;
    remot_info *premot = NULL;
    sonos_device device_info;

    result = espconn_get_connection_info(pesp_conn, &premot, 0);
    if(result != ESPCONN_OK) {
        os_printf("espconn_get_connection_info err=%d\n", result);
        return;
    }

#if 0
    os_printf("ssdp_recv_callback local=" IPSTR ":%d, remote=" IPSTR ":%d (len=%d)\n",
        IP2STR(pesp_conn->proto.udp->local_ip),
        pesp_conn->proto.udp->local_port,
        IP2STR(premot->remote_ip),
        premot->remote_port,
        length);
#endif

    if (!pusrdata) {
        os_printf("ssdp_recv_callback: null pusrdata\n");
        return;
    }

    os_bzero(&device_info, sizeof(sonos_device));
    device_info.ip[0] = premot->remote_ip[0];
    device_info.ip[1] = premot->remote_ip[1];
    device_info.ip[2] = premot->remote_ip[2];
    device_info.ip[3] = premot->remote_ip[3];

    char *ptemp = NULL;
    char *pdata = NULL;
    char line_buf[128];
    unsigned short line_len = 0;
    bool is_notify = false;
    bool is_sonos = false;

    pdata = pusrdata;
    ptemp = (char *)os_strstr(pdata, "\r\n");
    if (!ptemp) {
        os_printf("ssdp_recv_callback: missing line ending\n");
        return;
    }

    while (ptemp && pdata[0] != '\0') {
        // Copy the next line into a temporary buffer
        line_len = ptemp - pdata;
        if(line_len > sizeof(line_buf) - 1) {
            line_len = sizeof(line_buf) - 1;
        }
        os_memcpy(line_buf, pdata, line_len);
        line_buf[sizeof(line_buf) - 1] = '\0';

        if (pdata == pusrdata) {
            // Inspect the header line
            if (pesp_conn->proto.udp->local_port == SSDP_PORT) {
                if (line_len > 9 && os_strncmp(line_buf, "M-SEARCH ", 9) == 0) {
                    // Ignore search requests from other hosts
                    is_notify = false;
                }
                else if (line_len > 7 && os_strncmp(line_buf, "NOTIFY ", 7) == 0) {
                    is_notify = true;
                }

            }
            else if (pesp_conn->proto.udp->local_port == LOCAL_PORT) {
                if (line_len >= 15 && os_strncmp(line_buf, "HTTP/1.1 200 OK", 15) == 0) {
                    is_notify = true;   
                }
            }

            // Skip anything that lacks the expected header
            if (!is_notify) {
                break;
            }
        }
        else {
            // Inspect the body lines
            if (line_len > 8 && os_strncmp(line_buf, "SERVER: ", 8) == 0) {
                if (os_strstr(line_buf, "Sonos/")) {
                    is_sonos = true;
                } else {
                    is_sonos = false;
                    break;
                }
            }
            else if (line_len > 10 && os_strncmp(line_buf, "USN: uuid:", 10) == 0) {
                char *qtemp = os_strchr(line_buf + 10, ':');
                if (qtemp && qtemp > line_buf + 10) {
                    int n = qtemp - (line_buf + 10);
                    if (n > sizeof(device_info.uuid) - 1) {
                        os_printf("UUID too long: n=%d\n", n);
                        is_sonos = false;
                        break;
                    }

                    os_memcpy(device_info.uuid, line_buf + 10, n);
                    device_info.uuid[n] = '\0';
                }
            }
            else if (line_len > 10 && os_strncmp(line_buf, "LOCATION: ", 10) == 0) {
                if (os_strncmp(line_buf + 10, "http://", 7) == 0) {
                    char *qtemp, *rtemp;
                    char buf[128];
                    int n;

                    qtemp = os_strchr(line_buf + 17, ':');
                    if (qtemp && qtemp > line_buf + 17) {
                        n = qtemp - (line_buf + 17);
                        os_memcpy(buf, line_buf + 17, n);
                        buf[n] = '\0';

                        result = inet_pton(buf, device_info.ip);
                        if (result != 1) {
                            os_printf("IP parse error: %s\n", buf);
                            is_sonos = false;
                            break;
                        }
                    } else {
                        break;
                    }

                    rtemp = os_strchr(qtemp + 1, '/');
                    if (rtemp && rtemp - qtemp > 1) {
                        n = rtemp - qtemp - 1;
                        os_memcpy(buf, qtemp + 1, n);
                        buf[n] = '\0';

                        long int port_value = strtol(buf, NULL, 10);
                        if (port_value >= 0 && port_value <= UINT16_MAX) {
                            device_info.port = port_value;
                        } else {
                            os_printf("Port parse error: %s\n", buf);
                        }
                    } else {
                        break;
                    }
                }
            }
        }
        ptemp += 2;
        pdata = ptemp;
        ptemp = (char *)os_strstr(pdata, "\r\n");
    }

    // Abort if the message wasn't a notification from a Sonos device
    if (!is_notify || !is_sonos) {
        return;
    }

    // Abort if all relevant fields are not filled in
    if (device_info.port == 0 || device_info.uuid[0] == '\0'
        || device_info.ip[0] == 0 || device_info.ip[1] == 0
        || device_info.ip[2] == 0 || device_info.ip[3] == 0) {
        return;
    }

    bool existing_device = false;
    struct sonos_device_node *np;
    SLIST_FOREACH(np, &sonos_device_list, next) {
        if (os_strcmp(np->device.uuid, device_info.uuid) == 0) {
            if (os_memcmp(np->device.ip, device_info.ip, sizeof(device_info.ip)) != 0
                || np->device.port != device_info.port) {
                os_printf("Device info change for: %s\n", device_info.uuid);
                os_memcpy(&(np->device), &device_info, sizeof(sonos_device));
            }
            existing_device = true;
            break;
        }
    }

    if (!existing_device) {
        np = (struct sonos_device_node *)os_zalloc(sizeof(struct sonos_device_node));
        os_memcpy(&(np->device), &device_info, sizeof(sonos_device));
        SLIST_INSERT_HEAD(&sonos_device_list, np, next);
    }

    if (np->device.zone_name[0] == '\0' && !(np->zp_request_sent)) {
        //os_printf("Requesting ZP info\n");
        np->zp_request_sent = true;
        struct espconn *pespconn = (struct espconn *)os_zalloc(sizeof(struct espconn));
        pespconn->type = ESPCONN_TCP;
        pespconn->state = ESPCONN_NONE;
        pespconn->proto.tcp = (esp_tcp *)os_zalloc(sizeof(esp_tcp));
        pespconn->proto.tcp->local_port = espconn_port();
        pespconn->proto.tcp->remote_port = np->device.port;
        os_memcpy(pespconn->proto.tcp->remote_ip, np->device.ip, 4);
        espconn_regist_connectcb(pespconn, zp_request_connect_callback);
        espconn_regist_disconcb(pespconn, zp_request_disconnect_callback);
        espconn_regist_reconcb(pespconn, zp_request_reconnect_callback);
        pespconn->reverse = np;
        espconn_connect(pespconn);
    }
}

LOCAL void ICACHE_FLASH_ATTR zp_request_connect_callback(void *arg)
{
    struct espconn *pespconn = (struct espconn *)arg;

    //os_printf("zp_request_connect_callback\n");

    espconn_regist_sentcb(pespconn, zp_request_sent_callback);
    espconn_regist_recvcb(pespconn, zp_request_recv_callback);

    char *payload_buf = (char *)os_zalloc(PACKET_SIZE);
    if (!payload_buf) {
        return;
    }

    os_sprintf(payload_buf,
        "GET /status/zp HTTP/1.1\r\n"
        "Host: " IPSTR ":%d\r\n"
        "User-Agent: lwIP/1.4.0\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-cache\r\n"
        "\r\n",
        IP2STR(pespconn->proto.tcp->remote_ip),
        pespconn->proto.tcp->remote_port);

    espconn_sent(pespconn, (uint8 *)payload_buf, os_strlen(payload_buf));

    os_free(payload_buf);
}

LOCAL void ICACHE_FLASH_ATTR zp_request_sent_callback(void *arg)
{
    //os_printf("zp_request_sent_callback\n");
}

LOCAL void ICACHE_FLASH_ATTR zp_request_recv_callback(void *arg, char *pusrdata, unsigned short length)
{
    struct espconn *pespconn = (struct espconn *)arg;
    struct sonos_device_node *np = (struct sonos_device_node *)pespconn->reverse;

    //os_printf("zp_request_recv_callback: len=%d\n", length);

    if (pusrdata && length > 0) {
        if (!np->zp_response_buf) {
            np->zp_response_buf = (char *)os_malloc(PACKET_SIZE);
            np->zp_response_len = 0;
        }
        int n = MIN(length, PACKET_SIZE - np->zp_response_len - 1);
        if (n > 0) {
            os_memcpy(np->zp_response_buf + np->zp_response_len, pusrdata, n);
            np->zp_response_len += n;
            np->zp_response_buf[np->zp_response_len] = '\0';
        }
    }
}

LOCAL void ICACHE_FLASH_ATTR zp_request_disconnect_callback(void *arg)
{
    struct espconn *pespconn = (struct espconn *)arg;
    struct sonos_device_node *np = (struct sonos_device_node *)pespconn->reverse;
    char *ptemp = NULL;
    char *qtemp = NULL;

    //os_printf("zp_request_disconnect_callback\n");

    if (np && np->zp_response_buf && np->zp_response_len > 0) {
        ptemp = np->zp_response_buf;
        if (np->zp_response_len > 17 && os_strncmp(ptemp, "HTTP/1.1 200 OK\r\n", 17) == 0) {
            ptemp += 17;
            ptemp = (char *)os_strstr(ptemp, "<ZoneName>");
            if (ptemp) {
                ptemp += 10;
                qtemp = (char *)os_strstr(ptemp, "</ZoneName>");
                if (qtemp) {
                    int n = MIN(qtemp - ptemp, 127);
                    os_memcpy(np->device.zone_name, ptemp, n);
                    np->device.zone_name[n] = '\0';
                }
            }
        }

        if(discovery_callback && os_strlen(np->device.zone_name) > 0) {
            discovery_callback(&np->device, discovery_callback_user_data);
        }
    }

    free_tcp_connection(pespconn);
}

LOCAL void ICACHE_FLASH_ATTR zp_request_reconnect_callback(void *arg, sint8 err)
{
    struct espconn *pespconn = (struct espconn *)arg;

    os_printf("zp_request_reconnect_callback, err=%d\n", err);

    free_tcp_connection(pespconn);
}

LOCAL void ICACHE_FLASH_ATTR ssdp_request_timer_callback(void)
{
    os_timer_disarm(&ssdp_request_timer);
    espconn_delete(&ssdp_request_conn);

    os_printf("Discovery complete\n");

    ssdp_request_lock = 0;

#if 0
    struct sonos_device_node *np;
    SLIST_FOREACH(np, &sonos_device_list, next) {
        os_printf("Sonos device:\n");
        os_printf("  Address: " IPSTR ":%d\n", IP2STR(np->device.ip), np->device.port);
        os_printf("  UUID: %s\n", np->device.uuid);
        os_printf("  Zone name: %s\n", np->device.zone_name);
        os_printf("----\n");
    }
#endif
}

LOCAL void ICACHE_FLASH_ATTR free_tcp_connection(struct espconn *pespconn)
{
    if (pespconn) {
        struct sonos_device_node *np = (struct sonos_device_node *)pespconn->reverse;
        if (np->zp_response_buf) {
            os_free(np->zp_response_buf);
            np->zp_response_buf = NULL;
        }
        np->zp_response_len = 0;
        np->zp_request_sent = false;
        pespconn->reverse = NULL;

        if (pespconn->proto.tcp) {
            os_free(pespconn->proto.tcp);
        }
        os_free(pespconn);
    }
}
