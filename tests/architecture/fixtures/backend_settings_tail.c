int main(int argc,char**argv){assert(argc==2);g_ai.initialized=true;g_ai.udp_fd=-1;save_error=-EIO;
 if(!strcmp(argv[1],"reset")){settings.hdmi_enabled=true;reset_tuya_iot_preferences();assert(!settings.hdmi_enabled && hdmi_calls==1 && !hdmi_enabled);assert(g_ai.tuya_iot_settings_dirty);}
 else if(!strcmp(argv[1],"shutdown")){g_ai.tuya_iot_settings_dirty=true;service_ai_deinit();assert(saves==1 && g_ai.tuya_iot_settings_dirty && !g_ai.initialized);return 0;}
 else {g_ai.tuya_iot_settings_dirty=true;g_ai.tuya_iot_settings_save_due_ms=clock_ms;
 if(!strcmp(argv[1],"offline"))g_ai.status.state=AI_STATE_NETWORK_UNAVAILABLE;
 service_ai_update();assert(saves==1 && g_ai.tuya_iot_settings_dirty);}
 int before=saves;service_ai_update();assert(saves==before);clock_ms+=999;service_ai_update();assert(saves==before);
 clock_ms++;service_ai_update();assert(saves==before+1 && g_ai.tuya_iot_settings_dirty);
 save_error=0;clock_ms+=1000;service_ai_update();assert(saves==before+2 && !g_ai.tuya_iot_settings_dirty && !g_ai.tuya_iot_settings_save_due_ms);
 service_ai_update();assert(saves==before+2);puts("PASS exact reset/update/deinit selected path");return 0;}
