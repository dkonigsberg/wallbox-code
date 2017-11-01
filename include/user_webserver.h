#ifndef USER_WEBSERVER_H
#define USER_WEBSERVER_H

#include <os_type.h>

#define SERVER_PORT 80

void user_webserver_init(uint32 port, bool config_mode);

#endif /* USER_WEBSERVER_H */