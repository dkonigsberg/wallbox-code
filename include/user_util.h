#ifndef USER_UTIL_H
#define USER_UTIL_H

#include <os_type.h>

int inet_pton(const char *src, uint8 dst[4]);
int str_to_seconds(const char *str);
void unescape_html_entities(char *str, int len);

int wb_selection_to_index(char letter, int number);
bool wb_index_to_selection(int index, char *letter, int *number);

#endif /* USER_UTIL_H */