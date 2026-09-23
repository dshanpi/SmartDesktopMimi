# Frontends

Frontends translate user intent into application commands and render state.
They do not access device nodes, vendor SDKs, credentials or update storage.
The existing LVGL application remains under `apps/lv_port_linux` during the
compatibility window and migrates here only after each V1 command has a tested
V2 equivalent.
