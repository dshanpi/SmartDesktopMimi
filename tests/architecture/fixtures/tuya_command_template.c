#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "cJSON.h"
typedef int BOOL_T;typedef int OPERATE_RET;
#define TRUE 1
#define FALSE 0
#define OPRT_OK 0
#define OPRT_NOT_SUPPORTED -2
#define PR_WARN(...) ((void)0)
static int calls,acks,outcome,last_wake;
static char response[256];
static int tuya_device_iot_state_update(const char*s){(void)s;return 0;}
static int __main_ui_handle_enter_free_chat(int wake){last_wake=wake;calls++;return outcome;}
static int __main_ui_handle_exit_free_chat(void){calls++;return outcome;}
static void __main_ui_bridge_send(const char*s){assert(calls==1);acks++;snprintf(response,sizeof(response),"%s",s);}
/* PRODUCTION_COMMAND_HANDLER */
int main(void){
calls=acks=0;outcome=0;__main_ui_cmd_handle("{\"cmd\":\"unknown\",\"nested\":{\"cmd\":\"exit_free_chat\"}}");assert(calls==0 && acks==0);
calls=acks=0;outcome=-7;__main_ui_cmd_handle("{\"cmd\":\"unknown\",\"nested\":{\"cmd\":\"exit_free_chat\"}}");assert(calls==0 && acks==0);
calls=acks=0;outcome=0;__main_ui_cmd_handle("{\"cmd\":\"exit_free_chat\"}");assert(calls==1 && acks==0);
calls=acks=0;outcome=-7;__main_ui_cmd_handle("{\"cmd\":\"exit_free_chat\"}");assert(calls==1 && acks==0);
calls=acks=0;outcome=0;__main_ui_cmd_handle("{\"cmd\":\"exit_free_chat\",\"request_id\":\"abc-12\"}");assert(calls==1 && acks==1);
{cJSON *r=cJSON_Parse(response);assert(r);assert(!strcmp(cJSON_GetObjectItemCaseSensitive(r,"request_id")->valuestring,"abc-12"));assert(!strcmp(cJSON_GetObjectItemCaseSensitive(r,"command_result")->valuestring,"exit_free_chat"));assert(cJSON_GetObjectItemCaseSensitive(r,"result")->valueint==outcome);cJSON_Delete(r);}
calls=acks=0;outcome=-7;__main_ui_cmd_handle("{\"cmd\":\"exit_free_chat\",\"request_id\":\"abc-12\"}");assert(calls==1 && acks==1);
{cJSON *r=cJSON_Parse(response);assert(r);assert(!strcmp(cJSON_GetObjectItemCaseSensitive(r,"request_id")->valuestring,"abc-12"));assert(!strcmp(cJSON_GetObjectItemCaseSensitive(r,"command_result")->valuestring,"exit_free_chat"));assert(cJSON_GetObjectItemCaseSensitive(r,"result")->valueint==outcome);cJSON_Delete(r);}
calls=acks=0;outcome=0;__main_ui_cmd_handle("{\"cmd\":\"enter_free_chat\",\"request_id\":\"xyz_12\",\"wake\":false}");assert(calls==1 && acks==1);
{cJSON *r=cJSON_Parse(response);assert(r);assert(!strcmp(cJSON_GetObjectItemCaseSensitive(r,"request_id")->valuestring,"xyz_12"));assert(!strcmp(cJSON_GetObjectItemCaseSensitive(r,"command_result")->valuestring,"enter_free_chat"));assert(cJSON_GetObjectItemCaseSensitive(r,"result")->valueint==outcome);cJSON_Delete(r);}
calls=acks=0;outcome=-7;__main_ui_cmd_handle("{\"cmd\":\"enter_free_chat\",\"request_id\":\"xyz_12\",\"wake\":false}");assert(calls==1 && acks==1);
{cJSON *r=cJSON_Parse(response);assert(r);assert(!strcmp(cJSON_GetObjectItemCaseSensitive(r,"request_id")->valuestring,"xyz_12"));assert(!strcmp(cJSON_GetObjectItemCaseSensitive(r,"command_result")->valuestring,"enter_free_chat"));assert(cJSON_GetObjectItemCaseSensitive(r,"result")->valueint==outcome);cJSON_Delete(r);}
calls=acks=0;outcome=0;__main_ui_cmd_handle("{\"cmd\":\"exit_free_chat\",\"cmd\":\"enter_free_chat\"}");assert(calls==0 && acks==0);
calls=acks=0;outcome=-7;__main_ui_cmd_handle("{\"cmd\":\"exit_free_chat\",\"cmd\":\"enter_free_chat\"}");assert(calls==0 && acks==0);
calls=acks=0;outcome=0;__main_ui_cmd_handle("{\"cmd\":\"exit_free_chat\",\"request_id\":\"\"}");assert(calls==0 && acks==0);
calls=acks=0;outcome=-7;__main_ui_cmd_handle("{\"cmd\":\"exit_free_chat\",\"request_id\":\"\"}");assert(calls==0 && acks==0);
calls=acks=0;outcome=0;__main_ui_cmd_handle("{\"cmd\":\"exit_free_chat\",\"request_id\":null}");assert(calls==0 && acks==0);
calls=acks=0;outcome=-7;__main_ui_cmd_handle("{\"cmd\":\"exit_free_chat\",\"request_id\":null}");assert(calls==0 && acks==0);
calls=acks=0;outcome=0;__main_ui_cmd_handle("{\"cmd\":\"exit_free_chat\",\"request_id\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}");assert(calls==0 && acks==0);
calls=acks=0;outcome=-7;__main_ui_cmd_handle("{\"cmd\":\"exit_free_chat\",\"request_id\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}");assert(calls==0 && acks==0);
calls=acks=0;outcome=0;__main_ui_cmd_handle("{\"cmd\":\"exit_free_chat\",\"request_id\":\"a b\"}");assert(calls==0 && acks==0);
calls=acks=0;outcome=-7;__main_ui_cmd_handle("{\"cmd\":\"exit_free_chat\",\"request_id\":\"a b\"}");assert(calls==0 && acks==0);
calls=acks=0;outcome=0;__main_ui_cmd_handle("{\"cmd\":\"exit_free_chat\",\"request_id\":\"a\",\"request_id\":\"b\"}");assert(calls==0 && acks==0);
calls=acks=0;outcome=-7;__main_ui_cmd_handle("{\"cmd\":\"exit_free_chat\",\"request_id\":\"a\",\"request_id\":\"b\"}");assert(calls==0 && acks==0);
calls=acks=0;outcome=0;__main_ui_cmd_handle("{\"cmd\":\"exit_free_chat\"} trailing");assert(calls==0 && acks==0);
calls=acks=0;outcome=-7;__main_ui_cmd_handle("{\"cmd\":\"exit_free_chat\"} trailing");assert(calls==0 && acks==0);
calls=acks=0;outcome=0;__main_ui_cmd_handle("[{\"cmd\":\"exit_free_chat\"}]");assert(calls==0 && acks==0);
calls=acks=0;outcome=-7;__main_ui_cmd_handle("[{\"cmd\":\"exit_free_chat\"}]");assert(calls==0 && acks==0);
calls=acks=0;outcome=0;__main_ui_cmd_handle("{\"cmd\":\"enter_free_chat\",\"wake\":\"false\"}");assert(calls==0 && acks==0);
calls=acks=0;outcome=-7;__main_ui_cmd_handle("{\"cmd\":\"enter_free_chat\",\"wake\":\"false\"}");assert(calls==0 && acks==0);
calls=acks=0;outcome=0;__main_ui_cmd_handle("{\"cmd\":\"enter_free_chat\",\"wake\":true,\"wake\":false}");assert(calls==0 && acks==0);
calls=acks=0;outcome=-7;__main_ui_cmd_handle("{\"cmd\":\"enter_free_chat\",\"wake\":true,\"wake\":false}");assert(calls==0 && acks==0);
puts("PASS 28 real cJSON exact-handler cases: typed top-level dispatch, bounded IDs, correlated result, malformed rejection, legacy path");return 0;}