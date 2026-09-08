#pragma once

#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

void web_portal_register_handlers(httpd_handle_t server);
void terminal_log_init(void);

#ifdef __cplusplus
}
#endif
