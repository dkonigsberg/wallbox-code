#include "user_util.h"

#include <os_type.h>
#include <osapi.h>

#define NS_INADDRSZ 4

LOCAL const char WB_LETTERS[] = "ABCDEFGHJKLMNPQRSTUV";

/*
 * Convert an IPv4 address from string to numeric form.
 *
 * Based on the FreeBSD implementation of inet_pton4().
 *
 * Returns 1 if `src' is a valid dotted quad, else 0.
 */
int ICACHE_FLASH_ATTR inet_pton(const char *src, uint8 dst[4])
{
    LOCAL const char digits[] = "0123456789";
    int saw_digit, octets, ch;
    uint8 tmp[NS_INADDRSZ], *tp;

    saw_digit = 0;
    octets = 0;
    *(tp = tmp) = 0;
    while ((ch = *src++) != '\0') {
        const char *pch;

        if ((pch = os_strchr(digits, ch)) != NULL) {
            uint32_t new = *tp * 10 + (pch - digits);

            if (saw_digit && *tp == 0)
                return (0);
            if (new > 255)
                return (0);
            *tp = new;
            if (!saw_digit) {
                if (++octets > 4)
                    return (0);
                saw_digit = 1;
            }
        } else if (ch == '.' && saw_digit) {
            if (octets == 4)
                return (0);
            *++tp = 0;
            saw_digit = 0;
        } else
            return (0);
    }
    if (octets < 4)
        return (0);
    os_memcpy(dst, tmp, NS_INADDRSZ);
    return (1);
}

int ICACHE_FLASH_ATTR str_to_seconds(const char *str)
{
    // Time format: H:MM:SS

    const char *pch = str;

    long int h = strtol(pch, NULL, 10);
    if (h < 0 || h > 24) { return -1; }

    pch = os_strchr(pch, ':');
    if(!pch) { return -1; }
    pch++;

    long int m = strtol(pch, NULL, 10);
    if (m < 0 || m > 59) { return -1; }

    pch = os_strchr(pch, ':');
    if(!pch) { return -1; }
    pch++;

    long int s = strtol(pch, NULL, 10);
    if (s < 0 || s > 59) { return -1; }

    int secs = h * 3600 + m * 60 + s;

    return secs;
}

void ICACHE_FLASH_ATTR unescape_html_entities(char *str, int len)
{
    // This function operates in-place
    int p = 0;
    int q = 0;
    while (q < len) {
        if (q + 4 <= len && os_strncmp(str + q, "&lt;", 4) == 0) {
            str[p] = '<';
            p++; q += 4;
        }
        else if (q + 4 <= len && os_strncmp(str + q, "&gt;", 4) == 0) {
            str[p] = '>';
            p++; q += 4;
        }
        else if (q + 6 <= len && os_strncmp(str + q, "&quot;", 6) == 0) {
            str[p] = '"';
            p++; q += 6;
        }
        else if (q + 5 <= len && os_strncmp(str + q, "&amp;", 5) == 0) {
            str[p] = '&';
            p++; q += 5;
        }
        else {
            str[p] = str[q];
            p++; q++;
        }
    }
    str[p] = '\0';
}

int ICACHE_FLASH_ATTR wb_selection_to_index(char letter, int number)
{
    if (number < 1 || number > 10) {
        return -1;
    }

    char *pos = os_strchr(WB_LETTERS, letter);
    if (!pos) {
        return -1;
    }
    return 10 * (pos - WB_LETTERS) + (number - 1);
}

bool ICACHE_FLASH_ATTR wb_index_to_selection(int index, char *letter, int *number)
{
    if (index < 0 || index > 199 || !letter || !number) {
        return false;
    }

    *letter = WB_LETTERS[index / 10];
    *number = (index % 10) + 1;
    return true;
}