#ifndef HDMI_MCP_APP_H
#define HDMI_MCP_APP_H

#include "../app.h"

extern AppDescriptor app_hdmi_mcp;

void hdmi_mcp_app_init(void);
void hdmi_mcp_app_close(void);
void hdmi_mcp_app_monitor_start(void);

#endif
