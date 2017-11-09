#include "user_webserver.h"

#include <ets_sys.h>
#include <os_type.h>
#include <osapi.h>
#include <mem.h>
#include <user_interface.h>
#include <espconn.h>
#include <sys/param.h>
#include <stdlib.h>

#include <libesphttpd/platform.h>
#include <libesphttpd/espfs.h>
#include <libesphttpd/httpd.h>
#include <libesphttpd/httpdespfs.h>
#include <libesphttpd/webpages-espfs.h>
#include <libesphttpd/cgiwifi.h>
#include <libesphttpd/captdns.h>

#include "user_config.h"
#include "user_wb_credit.h"
#include "user_wb_selection.h"
#include "user_sonos_discovery.h"
#include "user_sonos_client.h"
#include "user_util.h"

typedef struct wb_song_list_data {
    int track_pos;
} wb_song_list_data;

typedef struct wb_song_select_data {
    wallbox_type wallbox;
    char uri_base[256];
    char track_file[200][16];
    char *buf;
    int buf_len;
} wb_song_select_data;

LOCAL CgiStatus ICACHE_FLASH_ATTR tpl_about(HttpdConnData *connData, char *token, void **arg);
LOCAL CgiStatus ICACHE_FLASH_ATTR tpl_sonos(HttpdConnData *connData, char *token, void **arg);
LOCAL CgiStatus ICACHE_FLASH_ATTR tpl_wallbox(HttpdConnData *connData, char *token, void **arg);
LOCAL CgiStatus ICACHE_FLASH_ATTR cgi_credit(HttpdConnData *data);
LOCAL CgiStatus ICACHE_FLASH_ATTR cgi_sonos(HttpdConnData *data);
LOCAL CgiStatus ICACHE_FLASH_ATTR cgi_sonos_zone_list(HttpdConnData *data);
LOCAL CgiStatus ICACHE_FLASH_ATTR cgi_sonos_zone_select(HttpdConnData *data);
LOCAL CgiStatus ICACHE_FLASH_ATTR cgi_wb_song_list(HttpdConnData *data);
LOCAL CgiStatus ICACHE_FLASH_ATTR cgi_wb_song_select(HttpdConnData *data);

LOCAL const HttpdBuiltInUrl CONFIG_URL_MAP[] = {
    {"/", cgiRedirect, "/wifi.tpl"},
    {"/wifi", cgiRedirect, "/wifi.tpl"},
    {"/wifi/", cgiRedirect, "/wifi.tpl"},
    {"/wifiscan.cgi", cgiWiFiScan, NULL},
    {"/wifi.tpl", cgiEspFsTemplate, tplWlan},
    {"/connect.cgi", cgiWiFiConnect, NULL},
    {"/connstatus.cgi", cgiWiFiConnStatus, NULL},
    {"*", cgiEspFsHook, NULL},
    {NULL, NULL, NULL}
};

LOCAL const HttpdBuiltInUrl FIXED_URL_MAP[] = {
    {"/", cgiRedirect, "/index.html"},
    {"/about", cgiRedirect, "/about.tpl"},
    {"/about/", cgiRedirect, "/about.tpl"},
    {"/about.tpl", cgiEspFsTemplate, tpl_about},
    {"/wifi", cgiRedirect, "/wifi.tpl"},
    {"/wifi/", cgiRedirect, "/wifi.tpl"},
    {"/wifiscan.cgi", cgiWiFiScan, NULL},
    {"/wifi.tpl", cgiEspFsTemplate, tplWlan},
    {"/connect.cgi", cgiWiFiConnect, NULL},
    {"/connstatus.cgi", cgiWiFiConnStatus, NULL},
    {"/sonos", cgiRedirect, "/sonos.tpl"},
    {"/sonos/", cgiRedirect, "/sonos.tpl"},
    {"/sonos.tpl", cgiEspFsTemplate, tpl_sonos},
    {"/wallbox", cgiRedirect, "/wallbox.tpl"},
    {"/wallbox/", cgiRedirect, "/wallbox.tpl"},
    {"/wallbox.tpl", cgiEspFsTemplate, tpl_wallbox},
    {"/zonelist.cgi", cgi_sonos_zone_list, NULL},
    {"/zoneselect.cgi", cgi_sonos_zone_select, NULL},
    {"/songlist.cgi", cgi_wb_song_list, NULL},
    {"/songselect.cgi", cgi_wb_song_select, NULL},
    {"/control/credit", cgi_credit, NULL},
    {"/control/sonos", cgi_sonos, NULL},
    {"*", cgiEspFsHook, NULL},
    {NULL, NULL, NULL}
};

void ICACHE_FLASH_ATTR user_webserver_init(uint32 port, bool config_mode)
{
    if (config_mode) {
        os_printf("In configuration mode, starting captive DNS...\n");
        captdnsInit();
    }

    espFsInit((void*)(webpages_espfs_start));

    httpdInit(
        config_mode ? CONFIG_URL_MAP : FIXED_URL_MAP,
        port, HTTPD_FLAG_NONE);
}

LOCAL CgiStatus ICACHE_FLASH_ATTR tpl_about(HttpdConnData *connData, char *token, void **arg)
{
    char buf[128];
    if (!token) return HTTPD_CGI_DONE;

    os_bzero(buf, sizeof(buf));

    if (os_strcmp(token, "BuildDescribe") == 0) {
        os_sprintf(buf, "%s", BUILD_DESCRIBE);
    }
    else if (os_strcmp(token, "SdkVersion") == 0) {
        os_sprintf(buf, "%s", system_get_sdk_version());
    }
    else if (os_strcmp(token, "BootVersion") == 0) {
        os_sprintf(buf, "v1.%d", system_get_boot_version());
    }
    else if (os_strcmp(token, "CpuFreq") == 0) {
        os_sprintf(buf, "%dMHz", system_get_cpu_freq());
    }
    else if (os_strcmp(token, "UserBinAddr") == 0) {
        os_sprintf(buf, "0x%08X", system_get_userbin_addr());
    }
    else if (os_strcmp(token, "FlashSize") == 0) {
        switch(system_get_flash_size_map()) {
        case FLASH_SIZE_4M_MAP_256_256:
            os_sprintf(buf, "4Mbit (256KB+256KB)");
            break;
        case FLASH_SIZE_2M:
            os_sprintf(buf, "2Mbit");
            break;
        case FLASH_SIZE_8M_MAP_512_512:
            os_sprintf(buf, "8Mbit (512KB+512KB)");
            break;
        case FLASH_SIZE_16M_MAP_512_512:
            os_sprintf(buf, "16Mbit (512KB+512KB)");
            break;
        case FLASH_SIZE_32M_MAP_512_512:
            os_sprintf(buf, "32Mbit (512KB+512KB)");
            break;
        case FLASH_SIZE_16M_MAP_1024_1024:
            os_sprintf(buf, "16Mbit (1024KB+1024KB)");
            break;
        case FLASH_SIZE_32M_MAP_1024_1024:
            os_sprintf(buf, "32Mbit (1024KB+1024KB)");
            break;
        case FLASH_SIZE_32M_MAP_2048_2048:
            os_sprintf(buf, "32Mbit (2024KB+2024KB)");
            break;
        case FLASH_SIZE_64M_MAP_1024_1024:
            os_sprintf(buf, "64Mbit (1024KB+1024KB)");
            break;
        case FLASH_SIZE_128M_MAP_1024_1024:
            os_sprintf(buf, "128Mbit (1024KB+1024KB)");
            break;
        default:
            os_sprintf(buf, "Unknown");
            break;
        }
    }
    else if (os_strcmp(token, "IpAddress") == 0) {
        struct ip_info info;
        if (wifi_get_ip_info(STATION_IF, &info)) {
            os_sprintf(buf, "%d.%d.%d.%d",
                (info.ip.addr >> 0) & 0xff, (info.ip.addr >> 8) & 0xff,
                (info.ip.addr >> 16) & 0xff, (info.ip.addr >> 24) & 0xff);
        }
    }
    else if (os_strcmp(token, "MacAddress") == 0) {
        uint8 macaddr[6];
        if (wifi_get_macaddr(STATION_IF, macaddr)) {
            os_sprintf(buf, "%02x:%02x:%02x:%02x:%02x:%02x\n",
                macaddr[0], macaddr[1], macaddr[2],
                macaddr[3], macaddr[4], macaddr[5]);
        }
    }

    httpdSend(connData, buf, -1);
    return HTTPD_CGI_DONE;
}

LOCAL CgiStatus ICACHE_FLASH_ATTR tpl_sonos(HttpdConnData *connData, char *token, void **arg)
{
    char buf[128];
    if (!token) return HTTPD_CGI_DONE;

    if (os_strcmp(token, "ZoneName") == 0) {
        sonos_device device;
        if (user_sonos_client_get_device(&device)) {
            os_sprintf(buf, "%s", device.zone_name);
        }
        else {
            os_sprintf(buf, "<Not Selected>");
        }
    }
    else if (os_strcmp(token, "ZoneUUID") == 0) {
        sonos_device device;
        if (user_sonos_client_get_device(&device)) {
            os_sprintf(buf, "%s", device.uuid);
        }
        else {
            buf[0]='\0';
        }
    }

    httpdSend(connData, buf, -1);
    return HTTPD_CGI_DONE;
}

LOCAL CgiStatus ICACHE_FLASH_ATTR tpl_wallbox(HttpdConnData *connData, char *token, void **arg)
{
    char buf[256];
    if (!token) return HTTPD_CGI_DONE;

    os_bzero(buf, sizeof(buf));

    if (os_strcmp(token, "Wallbox") == 0) {
        switch(user_config_get_wallbox_type()) {
        case SEEBURG_3W1_100:
            os_strcpy(buf, "SEEBURG_3W1_100");
            break;
        case SEEBURG_V3WA_200:
            os_strcpy(buf, "SEEBURG_V3WA_200");
            break;
        case UNKNOWN_WALLBOX:
        default:
            os_strcpy(buf, "UNKNOWN_WALLBOX");
            break;
        }
    }
    else if (os_strcmp(token, "UriBase") == 0) {
        const char *uri_base = user_config_get_sonos_uri_base();
        os_strcpy(buf, uri_base);
    }

    httpdSend(connData, buf, -1);
    return HTTPD_CGI_DONE;
}

LOCAL CgiStatus ICACHE_FLASH_ATTR cgi_credit(HttpdConnData *data)
{
    int credit_result = -1;
    int len;
    char buf[128];
    
    if (!data->conn) {
        return HTTPD_CGI_DONE;
    }

    // Accept arguments via the URL or the POST data
    len = httpdFindArg(data->getArgs, "coin", buf, sizeof(buf));
    if (len < 0) {
        len = httpdFindArg(data->post->buff, "coin", buf, sizeof(buf));
    }
    
    // http://<ip>/control/credit?coin=<value>
    if (len > 0) {
        if (os_strcmp(buf, "5") == 0) {
            os_printf("Inserted nickel\n");
            credit_result = user_wb_credit_drop(NICKEL);
        } else if (os_strcmp(buf, "10") == 0) {
            os_printf("Inserted dime\n");
            credit_result = user_wb_credit_drop(DIME);
        } else if (os_strcmp(buf, "25") == 0) {
            os_printf("Inserted quarter\n");
            credit_result = user_wb_credit_drop(QUARTER);
        }
    }

    if (credit_result != 1) {
        os_printf("Credit drop error: %d\n", credit_result);
    }

    httpdRedirect(data, "/index.html");
    return HTTPD_CGI_DONE;
}


LOCAL CgiStatus ICACHE_FLASH_ATTR cgi_sonos(HttpdConnData *data)
{
    int len;
    char buf[128];
    
    if (!data->conn) {
        return HTTPD_CGI_DONE;
    }

    // http://<ip>/control/sonos?action=<X>
    len = httpdFindArg(data->getArgs, "action", buf, sizeof(buf));
    if (len < 0) {
        len = httpdFindArg(data->post->buff, "action", buf, sizeof(buf));
    }
    if (len > 0) {
        // http://<ip>/control/sonos?action={discover}
        if (os_strcmp(buf, "discover") == 0) {
            os_printf("Discovering SONOS devices...\n");
            user_sonos_discovery_start();
        }
        httpdRedirect(data, "/index.html");
        return HTTPD_CGI_DONE;
    }

    // http://<ip>/control/sonos?uuid="<Zone UUID>"
    len = httpdFindArg(data->getArgs, "uuid", buf, sizeof(buf));
    if (len < 0) {
        len = httpdFindArg(data->post->buff, "uuid", buf, sizeof(buf));
    }
    if (len > 0) {
        os_printf("Setting zone to \"%s\"\n", buf);
        if (user_sonos_client_set_device(buf)) {
            user_config_set_sonos_uuid(buf);
        }

        httpdRedirect(data, "/index.html");
        return HTTPD_CGI_DONE;
    }


    httpdRedirect(data, "/index.html");
    return HTTPD_CGI_DONE;
}

LOCAL CgiStatus ICACHE_FLASH_ATTR cgi_sonos_zone_list(HttpdConnData *data)
{
    LOCAL const char RESPONSE[] = "{\"result\": { \"inProgress\": \"0\", \"zones\": %s}}";
    int len;
    char *buf;
    char *json_devices;

    if (!user_sonos_discovery_json_devices(&json_devices)) {
        httpdStartResponse(data, 500);
        httpdEndHeaders(data);
        return HTTPD_CGI_DONE;
    }

    buf = (char *)os_malloc(os_strlen(RESPONSE) + os_strlen(json_devices) + 1);
    if (!buf) {
        os_free(json_devices);
        httpdStartResponse(data, 500);
        httpdEndHeaders(data);
        return HTTPD_CGI_DONE;
    }

    len = os_sprintf(buf, RESPONSE, json_devices);
    os_free(json_devices);

    httpdStartResponse(data, 200);
    httpdHeader(data, "Content-Type", "text/json");
    httpdEndHeaders(data);

    httpdSend(data, buf, len);

    os_free(buf);

    return HTTPD_CGI_DONE;
}

LOCAL CgiStatus ICACHE_FLASH_ATTR cgi_sonos_zone_select(HttpdConnData *data)
{
    int len;
    char buf[128];

    if (!data->conn) {
        return HTTPD_CGI_DONE;
    }

    len = httpdFindArg(data->post->buff, "uuid", buf, sizeof(buf));

    if (len > 0) {
        os_printf("Setting zone to \"%s\"\n", buf);
        if (user_sonos_client_set_device(buf)) {
            user_config_set_sonos_uuid(buf);
        }
    }

    httpdRedirect(data, "/index.html");
    return HTTPD_CGI_DONE;
}

LOCAL CgiStatus ICACHE_FLASH_ATTR cgi_wb_song_list(HttpdConnData *data)
{
    char buf[1536];
    int i = 0;
    int n = 0;
    char letter;
    int number;

    wb_song_list_data *state = (wb_song_list_data *)data->cgiData;

    if (!data->conn) {
        if (state) {
            os_free(state);
        }
        return HTTPD_CGI_DONE;
    }

    if (!state) {
        state = (wb_song_list_data *)os_zalloc(sizeof(wb_song_list_data));
        data->cgiData = state;
        httpdStartResponse(data, 200);
        httpdHeader(data, "Content-Type", "text/json");
        httpdEndHeaders(data);
    }

    if (state->track_pos == 0) {
        buf[n++] = '{';
    }
    for (i = state->track_pos; i < state->track_pos + 50; i++) {
        if (!wb_index_to_selection(i, &letter, &number)) {
            os_free(state);
            return HTTPD_CGI_DONE;
        }
        n += os_sprintf(buf + n, "\"%c%d\": \"%s\"%s",
            letter, number,
            user_config_get_sonos_track_file(i),
            (i < 199) ? ", " : "}");
    }
    state->track_pos = i;

    httpdSend(data, buf, n);

    if (state->track_pos < 200) {
        return HTTPD_CGI_MORE;
    } else {
        os_free(state);
        return HTTPD_CGI_DONE;
    }
}

LOCAL CgiStatus ICACHE_FLASH_ATTR cgi_wb_song_select(HttpdConnData *data)
{
    wb_song_select_data *state = (wb_song_select_data *)data->cgiData;

    if (!data->conn) {
        // Connection aborted. Clean up.
        if (state) {
            if (state->buf) {
                os_free(state->buf);
            }
            os_free(state);
        }
        return HTTPD_CGI_DONE;
    }

    if (!state) {
        state = (wb_song_select_data *)os_zalloc(sizeof(wb_song_select_data));
        if (!state) {
            os_printf("Cannot allocate song selection state\n");
            return HTTPD_CGI_DONE;
        }

        data->cgiData = state;
    }

    char *ptemp = NULL;
    char *qtemp = NULL;
    char *offset = NULL;
    int n = 0;

    if (state->buf && data->post->buff) {
        char *btemp = (char *)os_malloc(state->buf_len + data->post->buffLen + 1);
        if (!btemp) {
            os_free(state->buf);
            return HTTPD_CGI_DONE;
        }
        os_strncpy(btemp, state->buf, state->buf_len);
        os_strncpy(btemp + state->buf_len, data->post->buff, data->post->buffLen + 1);
        data->post->buff = btemp;
        data->post->buffLen = state->buf_len + data->post->buffLen;
        data->post->buffSize = state->buf_len + data->post->buffLen;
        os_free(state->buf);
        state->buf = NULL;
        state->buf_len = 0;
    }

    ptemp = data->post->buff;
    while(ptemp && *ptemp !='\n' && *ptemp!='\r' && *ptemp != 0) {
        offset = ptemp;
        if(os_strncmp(ptemp, "wallbox=", 8) == 0) {
            ptemp += 8;
            qtemp = (char *)os_strchr(ptemp, '&');
            if (!qtemp) { qtemp = ptemp + os_strlen(ptemp); }
            n = qtemp - ptemp;
            if (n >= 15 && os_strncmp(ptemp, "SEEBURG_3W1_100", 15) == 0) {
                state->wallbox = SEEBURG_3W1_100;
            }
            else if (n >= 16 && os_strncmp(ptemp, "SEEBURG_V3WA_200", 16) == 0) {
                state->wallbox = SEEBURG_V3WA_200;
            }
        }
        else if(os_strncmp(ptemp, "uri-base=", 9) == 0) {
            ptemp += 9;
            qtemp = (char *)os_strchr(ptemp, '&');
            if (!qtemp) { qtemp = ptemp + os_strlen(ptemp); }
            n = qtemp - ptemp;
            httpdUrlDecode(ptemp, n, state->uri_base, sizeof(state->uri_base));
        }
        else if(os_strncmp(ptemp, "song-", 5) == 0) {
            ptemp += 5;
            qtemp = (char *)os_strchr(ptemp, '=');
            if (qtemp) {
                n = qtemp - ptemp;
                if (n <= 3) {
                    char letter = ptemp[0];
                    int number = strtol(ptemp + 1, NULL, 10);
                    int index = wb_selection_to_index(letter, number);
                    ptemp = qtemp + 1;
                    if (ptemp && index >= 0) {
                        qtemp = (char *)os_strchr(ptemp, '&');
                        if (!qtemp) { qtemp = ptemp + os_strlen(ptemp); }
                        n = MIN(qtemp - ptemp, 15);
                        httpdUrlDecode(ptemp, n, state->track_file[index], sizeof(state->track_file[index]));
                    }
                }
            }
        }

        ptemp = (char *)os_strchr(ptemp, '&');
        if (ptemp) { ptemp++; }
    }

    if (data->post->received < data->post->len) {
        // Save unparsed data
        if (offset) {
            state->buf_len = os_strlen(offset);
            state->buf = (char *)os_malloc(state->buf_len + 1);
            if (!state->buf) {
                os_free(state);
                return HTTPD_CGI_DONE;
            }
            os_strcpy(state->buf, offset);
        }
        return HTTPD_CGI_MORE;
    } else {
        // Save all the data
        user_config_set_wallbox_type(state->wallbox);
        user_config_set_sonos_uri_base(state->uri_base);
        user_config_set_sonos_track_files(&state->track_file);

        // Update the active wallbox selection
        user_wb_set_wallbox_type(user_config_get_wallbox_type());

        os_free(state);
        if (state->buf) {
            os_free(state->buf);
        }

        httpdRedirect(data, "/index.html");
        return HTTPD_CGI_DONE;
    }
}
