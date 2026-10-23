#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>
typedef struct {bool tuya_ai_enabled,bluetooth_enabled,hdmi_enabled,led_enabled;int volume,brightness,led_brightness;} sys_settings_t;
static sys_settings_t settings;
typedef struct {int state,last_error_code;bool tuya_bound,control_center_running,sound_app_running;} ai_status_t;
static struct {bool initialized,tuya_iot_settings_dirty,manage_processes;int64_t tuya_iot_settings_save_due_ms,last_tuya_iot_sync_ms,last_rx_ms;int udp_fd,sound_app_pid,control_center_pid,tuya_pid;ai_status_t status;} g_ai;
static int saves,save_error,hdmi_calls;static bool hdmi_enabled;static int64_t clock_ms=1000;
#define AI_STATE_ERROR 1
#define AI_STATE_IDLE 0
#define AI_STATE_NETWORK_UNAVAILABLE 2
#define TUYA_IOT_STATE_INTERVAL_MS 1000
#define AI_ONLINE_TIMEOUT_MS 10000
#define APP_LOGI(...) ((void)0)
#define APP_LOGW(...) ((void)0)
static int64_t now_ms(void){return clock_ms;}
static sys_settings_t *sys_settings_get(void){return &settings;}
static int sys_settings_save_checked(void){saves++;return save_error;}
static void sys_settings_save(void){(void)sys_settings_save_checked();}
static int sys_volume_set_percent(int x){return 0;}
static int sys_backlight_set_percent(int x){return 0;}
static void service_bt_enable(bool x){}
static void service_hdmi_preview_set_enabled(bool x){hdmi_calls++;hdmi_enabled=x;}
static void service_led_set_enabled(bool x){}
static void service_led_set_brightness(int x){}
static void ensure_runtime_processes(void){}
static bool status_equal(const ai_status_t*a,const ai_status_t*b){return !memcmp(a,b,sizeof(*a));}
static void publish_status(void){}
static void handle_ai_message(const char*b,size_t n){}
static bool expire_exit_command(void){return false;}
static void service_ai_retry_bt_resume_if_needed(int64_t n){}
static bool is_tuya_runtime(void){return true;}
static void send_tuya_iot_state(void){}
static bool sound_app_is_running(void){return false;}
static void log_status_if_needed(bool x){}
static void service_ai_stop_listen(void){}
static void stop_child_process(int *p,const char*n){*p=0;}
