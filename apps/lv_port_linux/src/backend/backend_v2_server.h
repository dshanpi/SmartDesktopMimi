#ifndef AITVBOX_BACKEND_V2_SERVER_H
#define AITVBOX_BACKEND_V2_SERVER_H

#include "../platform/platform_capabilities.h"

int backend_v2_server_start(const aitvbox_platform_descriptor_t *platform);
void backend_v2_server_stop(void);

#endif
