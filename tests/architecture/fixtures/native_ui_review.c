#include "src/ui/desktop_update.h"
/* Real LVGL widgets with in-memory hardware and middleware fixtures.
 * Run through scripts/render_ui_review.py from the repository root.
 */
#include "lvgl/lvgl.h"
#include "lvgl/src/widgets/buttonmatrix/lv_buttonmatrix_private.h"
#include "src/ui/ui.h"
#include "src/ui/ui_fonts.h"
#include "src/ui/ui_i18n.h"
#include "src/ui/theme/theme.h"
#include "src/ui/desktop_all_apps.h"
#include "src/apps/pairing/pairing_app.h"
#include "src/system/cloud_pairing_status.h"
#include "src/apps/iot/iot_app.h"
#include "src/apps/voice/voice_app.h"
#include "src/ui/desktop_home_tokens.h"
#include "src/ui/desktop_status_bar.h"
#include "src/ui/desktop_clock.h"
#include "src/ui/desktop_capabilities.h"
#include "src/ui/desktop_dock.h"
#include "src/ui/lockscreen.h"
#include "src/system/settings.h"
#include "src/ui/desktop_widgets.h"
#include "src/ui/desktop_clock.h"
#include <time.h>
#include <stdatomic.h>
#include "src/system/backlight_control.h"
#include "src/system/volume_control.h"
#include "src/system/app_registry.h"
#include "src/middleware/middleware.h"
#include "src/apps/setting/setting_app.h"
#include "src/apps/wifi/wifi_app.c"
#include <assert.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
/* Transport fixtures are only linked into this host renderer. They never
 * read account configuration or call the real cloud. */
static bool review_iot_connected;
static unsigned review_iot_commands;
static const char *review_iot_reply="{\"accepted\":true,\"state_confirmed\":false}";
static unsigned review_iot_delay_us;
static const char *review_iot_devices_reply;
static int review_iot_devices_rc;
static atomic_uint review_iot_requests;
static const char *review_iot_name="TEST Lamp";
int __wrap_mimi_iot_request(const char *path,const char *command,char *out,size_t cap){
 atomic_fetch_add(&review_iot_requests,1);
 const char *list_reply=review_iot_devices_reply;int list_rc=review_iot_devices_rc;
 if(review_iot_delay_us)usleep(review_iot_delay_us);
 if(!review_iot_connected){snprintf(out,cap,"{\"error\":\"not_configured\"}");return -2;}
 if(command){review_iot_commands++;snprintf(out,cap,"%s",review_iot_reply);return 0;}
 if(!strcmp(path,"/v1/devices") && list_reply){snprintf(out,cap,"%s",list_reply);return list_rc;}
 if(strstr(path,"/functions"))snprintf(out,cap,"{\"control_allowed\":true,\"functions\":[{\"code\":\"switch\",\"type\":\"Boolean\",\"values\":\"{}\"}]}");
 else snprintf(out,cap,"{\"devices\":[{\"id\":\"fixture_lamp\",\"name\":\"%s\",\"online\":true,\"status\":[{\"code\":\"switch\",\"value\":false}]}]}",review_iot_name);
 return 0;
}
/* A local socket pair supplies the idle response; no device service is used. */
static bool review_hdmi_idle;
static int hdmi_control_connections, hdmi_mutations;
extern lv_obj_t *hdmi_switch_obj;
static int review_control_peer = -1;
int __wrap_connect(int fd, const struct sockaddr *address, socklen_t length) {
 (void)length;
 if(address->sa_family == AF_UNIX && !strcmp(((const struct sockaddr_un *)address)->sun_path, "/var/run/aitvbox/control.sock"))hdmi_control_connections++;
 if (review_hdmi_idle && address->sa_family == AF_UNIX &&
     !strcmp(((const struct sockaddr_un *)address)->sun_path, "/var/run/aitvbox/control.sock")) {
  int pair[2];
  if (review_control_peer >= 0) close(review_control_peer);
  assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
  assert(dup2(pair[0], fd) == fd); close(pair[0]); review_control_peer=pair[1];
  const char reply[]="{\"state\":\"idle\",\"history\":[]}\n";
  assert(write(review_control_peer, reply, sizeof(reply)-1) == sizeof(reply)-1);
  return 0;
 }
 errno=ECONNREFUSED;return -1;
}
FILE *__real_fopen(const char *path, const char *mode);
FILE *__wrap_fopen(const char *path, const char *mode) {
    if (strstr(path, "SarasaUiSC-")) {
        char local[1024];
        snprintf(local, sizeof(local), "apps/lv_port_linux/src/ui/font/%s", strrchr(path, '/') + 1);
        return __real_fopen(local, mode);
    }
    return __real_fopen(path, mode);
}
void __real_lv_lottie_set_src_file(lv_obj_t *object, const char *source);
void __wrap_lv_lottie_set_src_file(lv_obj_t *object, const char *source) {
 const char *name=strrchr(source,'/');assert(name);
 const char *groups[]={"Neutral & Thinking","Positive Emotions","Negative Emotions","Surprise & Reactions","Physical & Special States"};
 char path[1024];
 for(size_t i=0;i<sizeof(groups)/sizeof(groups[0]);i++) {
  snprintf(path,sizeof(path),"third_party/aitvbox_emotion_lottie_pack/%s/%s",groups[i],name+1);
  FILE *file=__real_fopen(path,"rb");if(file){fclose(file);__real_lv_lottie_set_src_file(object,path);return;}
 }
 fprintf(stderr,"Missing real emotion asset: %s\n",name);assert(false);
}
/* Fixed review clock: 2026-10-16 15:47 Asia/Shanghai. No system clock write. */
time_t __wrap_time(time_t *out) {time_t value=1792136820;if(out)*out=value;return value;}
int __wrap_mimi_iot_configuration_present(void) {return 0;}
static char review_layout_dir[256];
static void cleanup_review_layout(void) {
 char path[512];
 snprintf(path,sizeof(path),"%s/settings.widgets.json",review_layout_dir);unlink(path);
 snprintf(path,sizeof(path),"%s/settings.widgets.json.lock",review_layout_dir);unlink(path);
 rmdir(review_layout_dir);
}
static sys_settings_t settings = {.brightness=78,.volume=50,.wifi_enabled=true,.theme_preset_id=0};
sys_settings_t *sys_settings_get(void) {return &settings;}
static int saves, backlight=20, volume=40;
void sys_settings_save(void) {saves++;}
int sys_settings_save_checked(void) {saves++;return 0;}
int sys_backlight_get_percent(int *p) {*p=backlight;return 0;}
int sys_volume_get_percent(int *p) {*p=volume;return 0;}
int sys_backlight_set_percent(int p) {backlight=p;return 0;}
int sys_volume_set_percent(int p) {volume=p;return 0;}
static mw_subscribe_cb_t callbacks[TOPIC_MAX][10];
int mw_subscribe(mw_topic_t t,mw_subscribe_cb_t c) {for(int i=0;i<10;i++)if(!callbacks[t][i]){callbacks[t][i]=c;return 0;}return -1;}
int mw_unsubscribe(mw_topic_t t,mw_subscribe_cb_t c) {for(int i=0;i<10;i++)if(callbacks[t][i]==c)callbacks[t][i]=NULL;return 0;}
static int connect_count, ai_exit_count, ota_commands, last_ota_command, pairing_refreshes;
static wifi_cmd_t last_connect;
int mw_publish(mw_topic_t t,const void*d,uint32_t n,mw_direction_t dir) {
 (void)dir;
 if(t==TOPIC_HDMI_COMMAND && n==sizeof(hdmi_cmd_t) && ((const hdmi_cmd_t*)d)->action!=HDMI_CMD_GET_STATUS)hdmi_mutations++;
 if(t==TOPIC_CLOUD_COMMAND && n==sizeof(cloud_cmd_t) && ((const cloud_cmd_t*)d)->action==CLOUD_CMD_REFRESH_BIND_TOKEN)pairing_refreshes++;
 if(t==TOPIC_OTA_COMMAND && n==sizeof(ota_cmd_t)){ota_commands++;last_ota_command=((const ota_cmd_t*)d)->action;}
 if(t==TOPIC_AI_COMMAND && n==sizeof(ai_cmd_t) && ((const ai_cmd_t*)d)->action==AI_CMD_EXIT_FREE_CHAT)ai_exit_count++;
 if(t==TOPIC_WIFI_COMMAND && n==sizeof(wifi_cmd_t) && ((const wifi_cmd_t*)d)->action==CMD_CONNECT) {
   connect_count++;last_connect=*(const wifi_cmd_t*)d;
 }
 return 0;
}
void mw_get_status(mw_status_t*s) {memset(s,0,sizeof(*s));}
static int app_open_count;
static app_id_t last_app_open;
#ifndef REVIEW_REAL_APP_MANAGER
void app_manager_init(void) {}
void app_manager_open(app_id_t id) {last_app_open=id;app_open_count++;}
void app_manager_back_home(void) {}
void app_manager_set_home_screen(lv_obj_t*s) {(void)s;}
void app_manager_refresh_current(void) {}
void app_manager_show_hdmi_preview_screen(void) {}
void app_manager_hide_hdmi_preview_screen(void) {}
#else
bool sunxifb_set_transparent_overlay(bool transparent) {(void)transparent;return true;}
#endif
extern AppDescriptor app_ai;
#include "src/apps/ai/ai_free_chat_view.h"
extern AppDescriptor app_bt,app_ota,app_video,app_user,app_hdmi_mcp;
static void emit(mw_topic_t t, void *data, size_t size) {
 mw_msg_t msg={.topic=t,.data_len=size};memcpy(msg.data,data,size);
 for(int i=0;i<10;i++)if(callbacks[t][i])callbacks[t][i](&msg);
}
static int review_width = 1024, review_height = 768;
static unsigned char *pixels;
static void flush(lv_display_t*d,const lv_area_t*a,uint8_t*p) {
 for(int y=a->y1;y<=a->y2;y++)memcpy(pixels+(y*review_width+a->x1)*4,p+(y-a->y1)*(a->x2-a->x1+1)*4,(a->x2-a->x1+1)*4);
 lv_display_flush_ready(d);
}
static void save(const char*path) {
 lv_obj_update_layout(lv_scr_act());
 lv_refr_now(NULL);
 FILE*f=fopen(path,"wb");assert(f);fprintf(f,"P6\n%d %d\n255\n",review_width,review_height);
 for(int i=0;i<review_width*review_height;i++){unsigned char rgb[]={pixels[i*4+2],pixels[i*4+1],pixels[i*4]};fwrite(rgb,1,3,f);}fclose(f);
}
static void inspect(lv_obj_t*o) {
 if(lv_obj_has_flag(o,LV_OBJ_FLAG_HIDDEN))return;
 lv_area_t a;lv_obj_get_coords(o,&a);
 if(lv_obj_check_type(o,&lv_label_class))printf("LABEL %d %d %d %d %s\n",a.x1,a.y1,a.x2,a.y2,lv_label_get_text(o));
 for(uint32_t i=0;i<lv_obj_get_child_count(o);i++)inspect(lv_obj_get_child(o,i));
}
static lv_obj_t *find_label(lv_obj_t *root, const char *text) {
 if(lv_obj_check_type(root,&lv_label_class) && !strcmp(lv_label_get_text(root),text))return root;
 for(uint32_t i=0;i<lv_obj_get_child_count(root);i++){
  lv_obj_t *found=find_label(lv_obj_get_child(root,i),text);if(found)return found;
 }
 return NULL;
}
static lv_obj_t *find_label_prefix(lv_obj_t *root, const char *prefix) {
 if(lv_obj_check_type(root,&lv_label_class) && !strncmp(lv_label_get_text(root),prefix,strlen(prefix)))return root;
 for(uint32_t i=0;i<lv_obj_get_child_count(root);i++){
  lv_obj_t *found=find_label_prefix(lv_obj_get_child(root,i),prefix);if(found)return found;
 }return NULL;
}
static void test_sliders(lv_obj_t *root, int *index) {
 if(lv_obj_check_type(root,&lv_slider_class)) {
  int value=(*index)++ == 0 ? 64 : 37;
  int before=saves;
  lv_slider_set_value(root,value,LV_ANIM_OFF);lv_obj_send_event(root,LV_EVENT_VALUE_CHANGED,NULL);
  assert(saves==before);
  lv_obj_send_event(root,LV_EVENT_RELEASED,NULL);assert(saves==before+1);
 }
 for(uint32_t i=0;i<lv_obj_get_child_count(root);i++)test_sliders(lv_obj_get_child(root,i),index);
}
static void pump(void) {lv_tick_inc(40);lv_timer_handler();}
static void test_settings(void) {
 int index=0;test_sliders(lv_scr_act(),&index);
 assert(index==2 && settings.brightness==64 && settings.volume==37 && backlight==64 && volume==37);
 lv_obj_t *english=find_label(lv_scr_act(),"English");assert(english);
 lv_obj_send_event(lv_obj_get_parent(english),LV_EVENT_CLICKED,NULL);pump();pump();
 assert(!ui_i18n_is_zh());
 lv_obj_t *dark=find_label(lv_scr_act(),"Dark");assert(dark);
 lv_obj_send_event(lv_obj_get_parent(dark),LV_EVENT_CLICKED,NULL);pump();pump();
 assert(ui_theme_is_dark());
 for(int t=0;t<TOPIC_MAX;t++){
  int count=0;for(int i=0;i<10;i++)if(callbacks[t][i])count++;
  assert(count <= (t==TOPIC_CLOUD_STATUS ? 2 : 1));
 }
 /* Closing during a deferred rebuild must not resurrect the settings page. */
 english=find_label(lv_scr_act(),"English");assert(english);
 lv_obj_send_event(lv_obj_get_parent(english),LV_EVENT_CLICKED,NULL);
 app_setting.close();lv_obj_t *last=lv_scr_act();pump();pump();assert(lv_scr_act()==last);
 assert(callbacks[TOPIC_AI_STATUS][0]==NULL);
 puts("PASS settings: live sliders, release persistence, locale/theme rebuild, subscriptions, close cancellation");
}
static void test_password(void) {
 lv_obj_update_layout(password_modal);
 lv_area_t kb,ta,dialog;lv_obj_get_coords(password_kb,&kb);lv_obj_get_coords(password_ta,&ta);
 lv_obj_get_coords(lv_obj_get_parent(password_kb),&dialog);
 assert(kb.y1>ta.y2 && kb.y2<dialog.y2 && kb.y2<review_height && kb.y2-kb.y1>=240);
 const char *letters="abcdefghijklmnopqrstuvwxyz";
 const char *const *map=lv_keyboard_get_map_array(password_kb);
 for(const char *c=letters;*c;c++) {bool found=false;for(int i=0;map[i][0];i++)if(map[i][0]==*c && map[i][1]==0)found=true;assert(found);}
 lv_textarea_set_text(password_ta,"short");lv_obj_send_event(password_kb,LV_EVENT_READY,NULL);
 assert(password_modal && connect_count==0 && strlen(lv_label_get_text(password_error_label))>0);
 lv_textarea_set_text(password_ta,"review123");lv_obj_send_event(password_kb,LV_EVENT_READY,NULL);
 assert(!password_modal && !password_kb && !password_ta && connect_count==1);
 assert(!strcmp(last_connect.ssid,"100ASK-Lab") && !strcmp(last_connect.password,"review123"));
 app_wifi.close();
 puts("PASS password: keyboard bounds, full alphabet, short-password rejection, connect command, modal cleanup");
}
#include "native_lockscreen_checks.h"

static lv_obj_t* find_textarea(lv_obj_t*root){if(lv_obj_check_type(root,&lv_textarea_class))return root;for(uint32_t i=0;i<lv_obj_get_child_count(root);i++){lv_obj_t*v=find_textarea(lv_obj_get_child(root,i));if(v)return v;}return NULL;}
static void tap_screen(lv_indev_t *pointer, int x, int y) {
 touch_x=x;touch_y=y;lv_obj_update_layout(lv_screen_active());
 touch_state=LV_INDEV_STATE_PRESSED;lv_indev_read(pointer);pump();
 touch_state=LV_INDEV_STATE_RELEASED;lv_indev_read(pointer);pump();
 lv_tick_inc(500);pump();pump();
}
/* Scroll real widgets into view and use the pointer, rather than dispatching
 * click events to controls which could be clipped or covered. */
static void tap_widget(lv_obj_t *widget) {
 lv_obj_scroll_to_view_recursive(widget,LV_ANIM_OFF);pump();lv_obj_update_layout(widget);
 lv_area_t b;lv_obj_get_coords(widget,&b);
 assert(b.x1>=0 && b.y1>=0 && b.x2<review_width && b.y2<review_height);
 for(lv_obj_t*p=lv_obj_get_parent(widget);p;p=lv_obj_get_parent(p)) {
  lv_area_t a;lv_obj_get_content_coords(p,&a);
  if(lv_obj_has_flag(p,LV_OBJ_FLAG_SCROLLABLE))assert(b.x1>=a.x1 && b.x2<=a.x2 && b.y1>=a.y1 && b.y2<=a.y2);
 }
 lv_indev_t*p=lv_indev_create();lv_indev_set_type(p,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(p,touch_read);
 touch_state=LV_INDEV_STATE_RELEASED;tap_screen(p,(b.x1+b.x2)/2,(b.y1+b.y2)/2);lv_indev_delete(p);
}
static lv_obj_t* find_keyboard(lv_obj_t*root) {
 if(lv_obj_check_type(root,&lv_keyboard_class))return root;
 for(uint32_t i=0;i<lv_obj_get_child_count(root);i++){lv_obj_t*v=find_keyboard(lv_obj_get_child(root,i));if(v)return v;}return NULL;
}
static void test_voice_keyboard(void) {
 app_voice.init();pump();lv_obj_t*entry=find_textarea(lv_scr_act());assert(entry);
 lv_obj_t*body=lv_obj_get_parent(entry),*kb=find_keyboard(lv_scr_act());assert(kb);
 int original_height=lv_obj_get_height(body),before=app_open_count;
 tap_widget(entry);assert(!lv_obj_has_flag(kb,LV_OBJ_FLAG_HIDDEN));
 lv_area_t a,b,v;lv_obj_get_coords(entry,&a);lv_obj_get_coords(kb,&b);lv_obj_get_coords(body,&v);
 assert(a.y1>=v.y1 && a.y2<=v.y2 && v.y2<b.y1 && b.y2<review_height);
 lv_textarea_set_text(entry,"open settings");
 /* Find and press the actual on-screen ready key. */
 uint32_t count=((lv_buttonmatrix_t*)kb)->btn_cnt;bool pressed=false;
 for(uint32_t i=0;i<count;i++)if(!strcmp(lv_buttonmatrix_get_button_text(kb,i),LV_SYMBOL_OK)) {
  lv_area_t key=((lv_buttonmatrix_t*)kb)->button_areas[i];
  lv_indev_t*p=lv_indev_create();lv_indev_set_type(p,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(p,touch_read);
  touch_state=LV_INDEV_STATE_RELEASED;tap_screen(p,b.x1+(key.x1+key.x2)/2,b.y1+(key.y1+key.y2)/2);lv_indev_delete(p);pressed=true;break;
 }
 assert(pressed && lv_obj_has_flag(kb,LV_OBJ_FLAG_HIDDEN));assert(lv_obj_get_height(body)==original_height);
 assert(app_open_count==before); /* Ready closes input; it never executes. */
 lv_obj_t*l=find_label(lv_scr_act(),ui_i18n_is_zh()?"预览文字指令":"Preview command");assert(l);tap_widget(lv_obj_get_parent(l));
 l=find_label(lv_scr_act(),ui_i18n_is_zh()?"确认执行":"Confirm action");assert(l);tap_widget(lv_obj_get_parent(l));assert(app_open_count==before+1);
 tap_widget(entry);assert(!lv_obj_has_flag(kb,LV_OBJ_FLAG_HIDDEN));
 puts("PASS voice keyboard: input remains above keyboard, ready restores viewport, pointer preview/confirm, reopening works");
}
static void test_launcher_rounds(void) {
 lv_indev_t *pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);
 lv_indev_set_read_cb(pointer,touch_read);touch_state=LV_INDEV_STATE_RELEASED;
 lv_obj_t *home=lv_screen_active();
 for(int round=0;round<100;round++) {
  for(size_t i=0;i<app_registry_get_launcher_count();i++) {
   const app_registry_entry_t *entry=app_registry_get_launcher_entry(i);
   ui_all_apps_show();pump();lv_tick_inc(250);pump();lv_obj_update_layout(home);
   lv_obj_t *label=NULL;
   for(int child=(int)lv_obj_get_child_count(home)-1;child>=0 && !label;child--) {
    lv_obj_t *layer=lv_obj_get_child(home,child);
    if(lv_obj_is_visible(layer))label=find_label(layer,ui_tr(entry->name_id));
   }
   assert(label);lv_obj_scroll_to_view_recursive(label,LV_ANIM_OFF);pump();assert(lv_obj_is_visible(label));
   lv_area_t box;lv_obj_get_coords(label,&box);
   tap_screen(pointer,(box.x1+box.x2)/2,(box.y1+box.y2)/2);
   assert(!ui_all_apps_is_visible() && lv_screen_active()!=home);
   ui_string_id_t title_id=entry->id==APP_ID_VIDEO?UI_STR_VIDEO_PLAYER:entry->name_id;
   if(!find_label(lv_screen_active(),ui_tr(title_id))){fprintf(stderr,"Missing title round=%d app=%s\n",round,entry->name);inspect(lv_screen_active());fflush(stdout);assert(false);}
   app_manager_back_home();lv_tick_inc(500);pump();pump();
   assert(lv_screen_active()==home);
  }
 }
 lv_indev_delete(pointer);
 puts("PASS 100 host pointer rounds across every registered launcher app and return to Home");
}
static void test_home_navigation(void) {
 lv_obj_t *home=lv_screen_active();
 lv_indev_t *pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);
 lv_indev_set_read_cb(pointer,touch_read);touch_state=LV_INDEV_STATE_RELEASED;
 pump();
 lv_obj_t *setting=find_label(home,ui_tr(UI_STR_APP_SETTINGS));assert(setting);
 lv_obj_t *card=lv_obj_get_parent(setting), *page=lv_obj_get_parent(card), *strip=lv_obj_get_parent(page);
 lv_obj_t *panel=lv_obj_get_parent(strip);
 lv_area_t panel_box;lv_obj_get_coords(panel,&panel_box);
 assert(panel_box.x1==HOME_CONTENT_X && panel_box.y1==HOME_DOCK_Y);
 assert(panel_box.x2<review_width && panel_box.y2<review_height);
 assert(lv_obj_get_child_count(strip)>1 && !ui_all_apps_is_visible());
 lv_obj_t *pager=lv_obj_get_child(panel,1);
 int32_t pages=(int32_t)lv_obj_get_child_count(strip);
 assert(lv_obj_get_child_count(pager)==(uint32_t)pages);
 assert(!lv_obj_check_type(pager,&lv_label_class));
 /* Every populated application page has one line; no label or number is used. */
 for(int32_t i=0;i<pages;i++) {
  lv_obj_t *line=lv_obj_get_child(pager,i);
  assert(!lv_obj_check_type(line,&lv_label_class));
  assert(lv_obj_get_width(line)>lv_obj_get_height(line));
 }
 lv_obj_scroll_to_x(strip,0,LV_ANIM_OFF);pump();
 /* Actual pointer drag must scroll inside the home strip, never activate an
  * app, hide the main cards or open the old full-screen launcher. */
 lv_area_t box;lv_obj_get_content_coords(strip,&box);
 touch_x=box.x2-24;touch_y=(box.y1+box.y2)/2;
 touch_state=LV_INDEV_STATE_PRESSED;lv_indev_read(pointer);pump();
 for(int i=1;i<=24;i++) {
  touch_x=box.x2-24-(lv_area_get_width(&box)*2/3)*i/24;
  lv_tick_inc(18);lv_indev_read(pointer);lv_timer_handler();
 }
 touch_state=LV_INDEV_STATE_RELEASED;lv_indev_read(pointer);
 for(int i=0;i<20;i++){lv_tick_inc(50);pump();}
 assert(abs(lv_obj_get_scroll_x(strip)-lv_obj_get_content_width(strip))<=2);
 for(int32_t i=0;i<pages;i++)
  assert(lv_obj_get_style_bg_opa(lv_obj_get_child(pager,i),0)==(i==1?LV_OPA_COVER:LV_OPA_30));
 assert(lv_screen_active()==home && !ui_all_apps_is_visible());
 /* Reverse drag returns exactly one page, at hardware replay speed. */
 touch_x=box.x1+24;touch_state=LV_INDEV_STATE_PRESSED;lv_indev_read(pointer);pump();
 for(int i=1;i<=24;i++) {
  touch_x=box.x1+24+(lv_area_get_width(&box)*2/3)*i/24;
  lv_tick_inc(18);lv_indev_read(pointer);lv_timer_handler();
 }
 touch_state=LV_INDEV_STATE_RELEASED;lv_indev_read(pointer);
 for(int i=0;i<20;i++){lv_tick_inc(50);pump();}
 assert(abs(lv_obj_get_scroll_x(strip))<=2);
 for(int32_t i=0;i<pages;i++)
  assert(lv_obj_get_style_bg_opa(lv_obj_get_child(pager,i),0)==(i==0?LV_OPA_COVER:LV_OPA_30));
 assert(lv_screen_active()==home && !ui_all_apps_is_visible());
 /* Every registry app is reachable directly from this strip. */
 for(size_t i=0;i<app_registry_get_launcher_count();i++) {
  const app_registry_entry_t *entry=app_registry_get_launcher_entry(i);
  lv_obj_t *label=find_label(panel,ui_tr(entry->name_id));assert(label);
  tap_widget(lv_obj_get_parent(label));
  assert(lv_screen_active()!=home && !ui_all_apps_is_visible());
  app_manager_back_home();lv_tick_inc(500);pump();pump();assert(lv_screen_active()==home);
 }
 lv_obj_scroll_to_x(strip,0,LV_ANIM_OFF);pump();
 puts("PASS inline home strip: pointer swipe stays on Home; every app opens directly and returns; no duplicate navigation");
 tap_screen(pointer,review_width/2,review_width<1000?84:32);
 lv_obj_t *center=find_label(lv_layer_top(),ui_tr(UI_STR_CONTROL_CENTER));
 assert(center && lv_obj_is_visible(center));
 tap_screen(pointer,review_width/2,review_height-16);
 lv_indev_delete(pointer);
 puts("PASS real Home pointer navigation: Settings, Apps, top-bar Control Center reach their own targets");
}

/* Scroll each primary action into view and verify actual hit testing at every
 * rendered size. This is host geometry evidence, never display qualification. */
static void test_responsive_home(void) {
 lv_obj_t *home=lv_screen_active();
 lv_indev_t *pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);
 lv_indev_set_read_cb(pointer,touch_read);touch_state=LV_INDEV_STATE_RELEASED;
 lv_obj_t *brand=find_label(home,"Mimi");assert(brand);pump();assert(lv_obj_get_height(brand)<=62);
 const ui_string_id_t actions[]={UI_STR_HOME_TALK_MIMI,desktop_hdmi_supported()?UI_STR_HOME_OPEN_KVM:UI_STR_HDMI_DETAILS};
 for(int i=0;i<2;i++) {
  lv_obj_t *label=find_label(home,ui_tr(actions[i]));assert(label);
  lv_obj_t *button=lv_obj_get_parent(label);
  lv_obj_scroll_to_view_recursive(button,LV_ANIM_OFF);pump();
  lv_area_t b,t;lv_obj_get_coords(button,&b);lv_obj_get_coords(label,&t);
  assert(b.x1>=0 && b.x2<review_width && b.y1>=HOME_STATUS_H && b.y2<HOME_DOCK_Y);
  assert(t.x1>=b.x1 && t.x2<=b.x2 && t.y1>=b.y1 && t.y2<=b.y2);
  tap_screen(pointer,(b.x1+b.x2)/2,(b.y1+b.y2)/2);
  assert(lv_screen_active()!=home);
  app_manager_back_home();lv_tick_inc(500);pump();pump();
 }
 lv_obj_t *first=find_label(home,ui_tr(UI_STR_HOME_AI_ASSISTANT));assert(first);
 lv_obj_scroll_to_view_recursive(first,LV_ANIM_OFF);pump();
 lv_indev_delete(pointer);
 puts("PASS responsive Home: primary actions visible after scrolling and pointer navigation succeeds");
}

static void ui075_tap(lv_obj_t *obj,lv_indev_t *pointer)
{
 assert(obj);lv_obj_scroll_to_view_recursive(obj,LV_ANIM_OFF);pump();lv_obj_update_layout(obj);lv_area_t a;lv_obj_get_coords(obj,&a);
 tap_screen(pointer,(a.x1+a.x2)/2,(a.y1+a.y2)/2);pump();pump();
}
static void ui075_remove_iot(lv_indev_t *pointer)
{
 lv_obj_t *name=find_label(lv_scr_act(),ui_i18n_is_zh()?"智能设备":"Smart devices");assert(name);
 lv_obj_t *card=lv_obj_get_parent(name),*shield=lv_obj_get_child(card,-1),*minus=lv_obj_get_child(shield,-1);
 ui075_tap(minus,pointer);
 assert(!find_label(lv_scr_act(),ui_i18n_is_zh()?"智能设备":"Smart devices"));
}
static void ui075_open_library(lv_indev_t *pointer)
{
 lv_obj_t *empty=find_label(lv_scr_act(),ui_i18n_is_zh()?"添加组件":"Add widget");assert(empty);
 lv_obj_t *shield=lv_obj_get_child(lv_obj_get_parent(empty),-1);ui075_tap(shield,pointer);
 assert(find_label(lv_scr_act(),ui_i18n_is_zh()?"移除组件不会卸载应用":"Removing widgets does not uninstall apps"));
}
static lv_obj_t *ui075_card(const char *zh, const char *en)
{
 lv_obj_t *name=find_label(lv_scr_act(),ui_i18n_is_zh()?zh:en);assert(name);
 return lv_obj_get_parent(name);
}
static void ui075_drag(lv_indev_t *pointer,lv_obj_t *from,lv_obj_t *to)
{
 lv_obj_update_layout(lv_scr_act());lv_area_t a,b;lv_obj_get_coords(from,&a);lv_obj_get_coords(to,&b);
 int x=(a.x1+a.x2)/2,y=a.y1+80,tx=(b.x1+b.x2)/2,ty=b.y1+80;
 touch_x=x;touch_y=y;touch_state=LV_INDEV_STATE_PRESSED;lv_indev_read(pointer);pump();
 for(int i=1;i<=12;i++){touch_x=x+(tx-x)*i/12;touch_y=y+(ty-y)*i/12;lv_tick_inc(18);lv_indev_read(pointer);lv_timer_handler();}
 touch_state=LV_INDEV_STATE_RELEASED;lv_indev_read(pointer);pump();pump();
}
static void ui075_drag_and_library(lv_indev_t *pointer,const char *path)
{
 mimi_widget_layout loaded;
 int before=app_open_count,cloud_before=pairing_refreshes,hdmi_before=hdmi_mutations;
 /* Hold the real environment card; releasing after the editor rebuild must
  * neither open Settings nor leak a click to a control below. */
 lv_obj_t *env=ui075_card("环境","Environment");lv_area_t a;lv_obj_get_coords(env,&a);
 touch_x=(a.x1+a.x2)/2;touch_y=a.y1+80;touch_state=LV_INDEV_STATE_PRESSED;lv_indev_read(pointer);
 for(int i=0;i<45;i++){lv_tick_inc(20);lv_indev_read(pointer);lv_timer_handler();}
 touch_state=LV_INDEV_STATE_RELEASED;lv_indev_read(pointer);pump();pump();
 assert(find_label(lv_scr_act(),ui_i18n_is_zh()?"编辑桌面组件":"Edit desktop widgets"));
 ui075_drag(pointer,ui075_card("环境","Environment"),ui075_card("智能设备","Smart devices"));
 /* Cross-size drop is refused; the previous same-size swap remains. */
 ui075_drag(pointer,ui075_card("环境","Environment"),ui075_card("Mimi 助手","Mimi Assistant"));
 lv_obj_t *save=find_label(lv_scr_act(),ui_i18n_is_zh()?"保存布局":"Save layout");ui075_tap(lv_obj_get_parent(save),pointer);
 assert(!mimi_widget_layout_load(path,&loaded));
 assert(loaded.slots[0]==MIMI_WIDGET_ASSISTANT && loaded.slots[2]==MIMI_WIDGET_IOT && loaded.slots[3]==MIMI_WIDGET_ENVIRONMENT);
 assert(!unlink(path));ui_rebuild_home();pump();pump();
 desktop_widgets_edit();pump();pump();ui075_remove_iot(pointer);ui075_open_library(pointer);
 /* Existing and incompatible rows cannot write into the selected slot. */
 lv_obj_t *row=lv_obj_get_parent(find_label(lv_scr_act(),ui_i18n_is_zh()?"网络状态":"Network"));
 lv_obj_t *list=lv_obj_get_parent(row);
 ui075_tap(lv_obj_get_child(lv_obj_get_child(list,0),-1),pointer);
 assert(find_label(lv_scr_act(),ui_i18n_is_zh()?"移除组件不会卸载应用":"Removing widgets does not uninstall apps"));
 lv_obj_t *close=find_label(lv_scr_act(),LV_SYMBOL_CLOSE);assert(close);ui075_tap(lv_obj_get_parent(close),pointer);
 assert(!find_label(lv_scr_act(),ui_i18n_is_zh()?"移除组件不会卸载应用":"Removing widgets does not uninstall apps"));
 assert(find_label(lv_scr_act(),ui_i18n_is_zh()?"添加组件":"Add widget"));
 ui075_open_library(pointer);
 /* Save remains reachable while the library is open on the approved screen. */
 save=find_label(lv_scr_act(),ui_i18n_is_zh()?"保存布局":"Save layout");ui075_tap(lv_obj_get_parent(save),pointer);
 assert(!mimi_widget_layout_load(path,&loaded));assert(loaded.slots[3]==MIMI_WIDGET_EMPTY);
 assert(!unlink(path));ui_rebuild_home();pump();pump();
 assert(app_open_count==before && pairing_refreshes==cloud_before && hdmi_mutations==hdmi_before);
 puts("PASS UI075 real pointer long press, same-size drag, cross-size rejection, disabled row, close/reopen retains draft, modal-open save; no background actions");
}
static void test_widget_editor(void)
{
 int before=app_open_count,cloud_before=pairing_refreshes,hdmi_before=hdmi_mutations;
 lv_indev_t *pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(pointer,touch_read);touch_state=LV_INDEV_STATE_RELEASED;
 char path[4096];assert(!mimi_widget_layout_path(path,sizeof(path)));
 mimi_widget_layout initial,loaded;assert(!mimi_widget_layout_load(path,&initial));
 desktop_widgets_edit();pump();pump();ui075_remove_iot(pointer);
 lv_obj_t *cancel=find_label(lv_scr_act(),ui_i18n_is_zh()?"取消":"Cancel");ui075_tap(lv_obj_get_parent(cancel),pointer);
 assert(!mimi_widget_layout_load(path,&loaded));assert(!memcmp(&initial,&loaded,sizeof(initial)));
 assert(find_label(lv_scr_act(),ui_i18n_is_zh()?"智能设备":"Smart devices"));
 desktop_widgets_edit();pump();pump();ui075_remove_iot(pointer);ui075_open_library(pointer);
 lv_obj_t *network=find_label(lv_scr_act(),ui_i18n_is_zh()?"网络状态":"Network");assert(network);
 ui075_tap(lv_obj_get_child(lv_obj_get_parent(network),-1),pointer);
 assert(!find_label(lv_scr_act(),ui_i18n_is_zh()?"移除组件不会卸载应用":"Removing widgets does not uninstall apps"));
 lv_obj_t *save=find_label(lv_scr_act(),ui_i18n_is_zh()?"保存布局":"Save layout");ui075_tap(lv_obj_get_parent(save),pointer);
 assert(!mimi_widget_layout_load(path,&loaded));assert(loaded.slots[3]==MIMI_WIDGET_NETWORK && loaded.revision==1);
 ui_rebuild_home();pump();pump();assert(find_label(lv_scr_act(),ui_i18n_is_zh()?"网络状态":"Network"));
 /* Simulate an unsupported on-disk version. Saving must leave both it and the draft intact. */
 FILE *f=fopen(path,"w");assert(f);fputs("{\"schema\":99}",f);fclose(f);
 desktop_widgets_edit();pump();pump();save=find_label(lv_scr_act(),ui_i18n_is_zh()?"保存布局":"Save layout");ui075_tap(lv_obj_get_parent(save),pointer);
 assert(find_label(lv_scr_act(),ui_i18n_is_zh()?"保存失败，草稿已保留":"Save failed; draft retained"));
 assert(find_label(lv_scr_act(),ui_i18n_is_zh()?"保存布局":"Save layout"));
 cancel=find_label(lv_scr_act(),ui_i18n_is_zh()?"取消":"Cancel");ui075_tap(lv_obj_get_parent(cancel),pointer);
 assert(app_open_count==before && pairing_refreshes==cloud_before && hdmi_mutations==hdmi_before);
 assert(!unlink(path));ui_rebuild_home();pump();pump();ui075_drag_and_library(pointer,path);lv_indev_delete(pointer);
 puts("PASS UI075 actual pointer editing: remove/cancel, compatible add, save/reload, failed save retains draft; no app launch/pairing/HDMI mutation");
}

static void test_center_controls(void)
{
 int index=0;test_sliders(lv_layer_top(),&index);assert(index==2);
 assert(settings.brightness==64 && settings.volume==37 && backlight==64 && volume==37);
 lv_obj_t *mute_name=find_label(lv_layer_top(),ui_i18n_is_zh()?"静音":"Mute");assert(mute_name);
 lv_obj_t *mute=lv_obj_get_child(lv_obj_get_parent(mute_name),-1);assert(lv_obj_check_type(mute,&lv_switch_class));
 lv_obj_add_state(mute,LV_STATE_CHECKED);lv_obj_send_event(mute,LV_EVENT_VALUE_CHANGED,NULL);assert(volume==0);
 lv_obj_clear_state(mute,LV_STATE_CHECKED);lv_obj_send_event(mute,LV_EVENT_VALUE_CHANGED,NULL);assert(volume==37);
 if(desktop_hdmi_supported()){
  lv_obj_t *name=find_label(lv_layer_top(),ui_i18n_is_zh()?"HDMI 采集":"HDMI capture");assert(name);
  lv_obj_t *sw=lv_obj_get_child(lv_obj_get_parent(name),-1);assert(lv_obj_check_type(sw,&lv_switch_class));
  hdmi_preview_status_t status={0};emit(TOPIC_HDMI_PREVIEW_STATUS,&status,sizeof(status));pump();
  int before=saves,mutations=hdmi_mutations;
  lv_obj_add_state(sw,LV_STATE_CHECKED);lv_obj_send_event(sw,LV_EVENT_VALUE_CHANGED,NULL);
  assert(saves==before && hdmi_mutations==mutations+1 && !settings.hdmi_enabled);
  status.enabled=true;emit(TOPIC_HDMI_PREVIEW_STATUS,&status,sizeof(status));pump();assert(saves==before+1 && settings.hdmi_enabled && lv_obj_has_state(sw,LV_STATE_CHECKED));
  lv_obj_clear_state(sw,LV_STATE_CHECKED);lv_obj_send_event(sw,LV_EVENT_VALUE_CHANGED,NULL);assert(saves==before+1);
  status.enabled=false;emit(TOPIC_HDMI_PREVIEW_STATUS,&status,sizeof(status));pump();assert(!settings.hdmi_enabled && saves==before+2);
  lv_obj_add_state(sw,LV_STATE_CHECKED);lv_obj_send_event(sw,LV_EVENT_VALUE_CHANGED,NULL);lv_tick_inc(2600);pump();
  assert(saves==before+2 && !settings.hdmi_enabled && !lv_obj_has_state(sw,LV_STATE_CHECKED));
  assert(find_label(lv_layer_top(),ui_i18n_is_zh()?"HDMI 未响应，请检查连接":"HDMI did not respond; check connection"));
 }
 puts("PASS UI075 controls: sliders apply/read back/save on release; mute restores volume; HDMI request does not save before ACK; timeout does not report success");
}

static void test_environment_details(bool stale_view)
{
 int apps=app_open_count,updates=ota_commands,bindings=pairing_refreshes,capture=hdmi_mutations,stored=saves;
 lv_indev_t *pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(pointer,touch_read);
 sensor_status_t sensor={.valid=true,.temp_mC=30000,.humi_mpermil=36000};
 emit(TOPIC_SENSOR_STATUS,&sensor,sizeof(sensor));pump();
 assert(find_label(lv_scr_act(),"30°C") && find_label(lv_scr_act(),"36%"));
 lv_tick_inc(61000);pump();pump();
 assert(find_label(lv_scr_act(),ui_i18n_is_zh()?"数据已过期":"Data is stale"));
 assert(!find_label(lv_scr_act(),"30°C") && !find_label(lv_scr_act(),"36%"));
 emit(TOPIC_SENSOR_STATUS,&sensor,sizeof(sensor));pump();
 ui075_tap(ui075_card("环境","Environment"),pointer);
 assert(find_label(lv_layer_top(),ui_i18n_is_zh()?"环境详情":"Environment details"));
 assert(find_label(lv_layer_top(),"30°C     36%"));
 /* Covered Home card and control-bar touches must not activate apps. */
 tap_screen(pointer,HOME_SCREEN_W-80,32);
 assert(app_open_count==apps && find_label(lv_layer_top(),ui_i18n_is_zh()?"环境详情":"Environment details"));
 lv_tick_inc(61000);pump();pump();
 assert(find_label(lv_scr_act(),ui_i18n_is_zh()?"数据已过期":"Data is stale"));
 assert(find_label(lv_layer_top(),ui_i18n_is_zh()?"数据已过期，等待新的读数":"Data is stale; waiting for a new reading"));
 assert(!find_label(lv_layer_top(),"30°C     36%"));
 sensor.temp_mC=27000;sensor.humi_mpermil=43000;emit(TOPIC_SENSOR_STATUS,&sensor,sizeof(sensor));pump();
 assert(find_label(lv_layer_top(),"27°C     43%"));
 sensor.err_code=-5;emit(TOPIC_SENSOR_STATUS,&sensor,sizeof(sensor));pump();
 assert(find_label(lv_layer_top(),ui_i18n_is_zh()?"当前读数不可用":"Current reading unavailable"));
 sensor.err_code=0;sensor.humi_mpermil=110000;emit(TOPIC_SENSOR_STATUS,&sensor,sizeof(sensor));pump();
 assert(find_label(lv_layer_top(),ui_i18n_is_zh()?"当前读数不可用":"Current reading unavailable"));
 sensor.humi_mpermil=43000;emit(TOPIC_SENSOR_STATUS,&sensor,sizeof(sensor));pump();
 lv_obj_t *close=find_label(lv_layer_top(),ui_i18n_is_zh()?"关闭":"Close");ui075_tap(lv_obj_get_parent(close),pointer);
 assert(!find_label(lv_layer_top(),ui_i18n_is_zh()?"环境详情":"Environment details"));
 ui075_tap(ui075_card("环境","Environment"),pointer);
 ui_rebuild_home();pump();pump();
 assert(!find_label(lv_layer_top(),ui_i18n_is_zh()?"环境详情":"Environment details"));
 ui075_tap(ui075_card("环境","Environment"),pointer);
 assert(find_label(lv_layer_top(),"27°C     43%"));
 if(stale_view){lv_tick_inc(61000);pump();pump();}
 assert(app_open_count==apps && ota_commands==updates && pairing_refreshes==bindings && hdmi_mutations==capture && saves==stored);
 lv_indev_delete(pointer);
 puts("PASS UI075 environment: actual card tap, overlay blocking, expiry, valid recovery, provider error/out-of-range rejection, close/reopen and Home rebuild cleanup; no commands/saves");
}
static void test_notice(bool leave_error)
{
 lv_indev_t *pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(pointer,touch_read);
 tap_screen(pointer,review_width/2,32);
 int apps=app_open_count,updates=ota_commands,bindings=pairing_refreshes,capture=hdmi_mutations,stored=saves;
 ota_status_t status={.state=OTA_STATE_IDLE};emit(TOPIC_OTA_STATUS,&status,sizeof(status));lv_tick_inc(1100);pump();
 lv_obj_t *l=find_label(lv_layer_top(),ui_i18n_is_zh()?"通知    暂无本地通知":"Notifications    No local notifications");assert(l);
 ui075_tap(lv_obj_get_parent(l),pointer);assert(app_open_count==apps);
 status.state=OTA_STATE_DOWNLOADING;status.progress=42;emit(TOPIC_OTA_STATUS,&status,sizeof(status));lv_tick_inc(1100);pump();
 l=find_label(lv_layer_top(),ui_i18n_is_zh()?"系统更新 · 下载中 42%":"System update · Downloading 42%");assert(l);
 ui075_tap(lv_obj_get_parent(l),pointer);assert(app_open_count==apps+1 && last_app_open==APP_ID_OTA);
 assert(ota_commands==updates && pairing_refreshes==bindings && hdmi_mutations==capture && saves==stored);
 tap_screen(pointer,review_width/2,32);
 status.state=OTA_STATE_UP_TO_DATE;emit(TOPIC_OTA_STATUS,&status,sizeof(status));lv_tick_inc(1100);pump();
 assert(find_label(lv_layer_top(),ui_i18n_is_zh()?"通知    暂无本地通知":"Notifications    No local notifications"));
 status.state=OTA_STATE_ERROR;status.error_code=-5;emit(TOPIC_OTA_STATUS,&status,sizeof(status));lv_tick_inc(1100);pump();
 assert(find_label(lv_layer_top(),ui_i18n_is_zh()?"系统更新 · 更新失败，点此查看":"System update · Update failed; tap for details"));
 if(!leave_error){status.state=OTA_STATE_IDLE;emit(TOPIC_OTA_STATUS,&status,sizeof(status));lv_tick_inc(1100);pump();}
 lv_indev_delete(pointer);
 puts("PASS UI075 notification: existing OTA event, empty row disabled, download progress, click only opens system OTA, up-to-date clears and failure remains visible; no mutation");
}

static mimi_iot_summary ui075_fetch_summary(const char *reply,int rc)
{
 review_iot_devices_reply=reply;review_iot_devices_rc=rc;review_iot_connected=true;
 app_iot.init();for(int i=0;i<30;i++){usleep(10000);lv_tick_inc(100);pump();}
 mimi_iot_summary result=mimi_iot_summary_get();app_iot.close();return result;
}
static void test_iot_summary(void)
{
 lv_obj_t *home=lv_scr_act();
 int commands=review_iot_commands,bindings=pairing_refreshes,capture=hdmi_mutations;
 assert(mimi_iot_summary_get().state==MIMI_IOT_SUMMARY_UNKNOWN);
 mimi_iot_summary snap=ui075_fetch_summary("{\"devices\":[]}",0);
 assert(snap.state==MIMI_IOT_SUMMARY_READY && snap.total==0 && snap.online==0);
 snap=ui075_fetch_summary("{\"devices\":[{\"id\":\"lamp\",\"online\":true},{\"id\":\"socket\",\"online\":false}]}",0);
 assert(snap.state==MIMI_IOT_SUMMARY_READY && snap.total==2 && snap.online==1);
 lv_scr_load(home);lv_tick_inc(1100);pump();
 assert(find_label(home,ui_i18n_is_zh()?"2 台设备 · 1 台在线":"2 devices · 1 online"));
 unsigned requests=atomic_load(&review_iot_requests);
 lv_tick_inc(61000);pump();pump();snap=mimi_iot_summary_get();
 assert(snap.state==MIMI_IOT_SUMMARY_STALE && snap.total==0 && snap.online==0);
 assert(find_label(home,ui_i18n_is_zh()?"状态已过期":"Status is stale"));
 assert(atomic_load(&review_iot_requests)==requests);
 snap=ui075_fetch_summary("{\"devices\":[{\"id\":\"lamp\"}]}",0);assert(snap.state==MIMI_IOT_SUMMARY_UNAVAILABLE);
 snap=ui075_fetch_summary("{\"devices\":[{\"id\":\"lamp\",\"online\":true},{\"id\":\"lamp\",\"online\":false}]}",0);assert(snap.state==MIMI_IOT_SUMMARY_UNAVAILABLE);
 snap=ui075_fetch_summary("{\"devices\":[42,null]}",0);assert(snap.state==MIMI_IOT_SUMMARY_UNAVAILABLE);
 snap=ui075_fetch_summary("{\"error\":\"authorization_required\"}",-2);assert(snap.state==MIMI_IOT_SUMMARY_UNAUTHORIZED && snap.total==0);
 snap=ui075_fetch_summary("{\"error\":\"not_configured\"}",-2);assert(snap.state==MIMI_IOT_SUMMARY_NOT_CONFIGURED);
 snap=ui075_fetch_summary("{\"devices\":[]} trailing",0);assert(snap.state==MIMI_IOT_SUMMARY_UNAVAILABLE);
 /* A completed response for an already-closed page must not populate Home. */
 review_iot_devices_reply="{\"devices\":[]}";review_iot_devices_rc=0;review_iot_delay_us=150000;
 app_iot.init();app_iot.close();
 for(int i=0;i<25;i++){usleep(10000);lv_tick_inc(100);pump();}
 assert(mimi_iot_summary_get().state==MIMI_IOT_SUMMARY_UNAVAILABLE);
 review_iot_delay_us=0;
 snap=ui075_fetch_summary("{\"devices\":[{\"id\":\"lamp\",\"online\":true},{\"id\":\"socket\",\"online\":false}]}",0);
 assert(snap.state==MIMI_IOT_SUMMARY_READY && snap.total==2 && snap.online==1);
 lv_scr_load(home);ui_rebuild_home();pump();pump();requests=atomic_load(&review_iot_requests);
 lv_tick_inc(1100);pump();assert(find_label(lv_scr_act(),ui_i18n_is_zh()?"2 台设备 · 1 台在线":"2 devices · 1 online"));
 assert(atomic_load(&review_iot_requests)==requests && review_iot_commands==commands && pairing_refreshes==bindings && hdmi_mutations==capture);
 puts("PASS UI075 IoT aggregate: real worker/parser, empty/online counts, absent online and duplicate IDs rejected, auth/config/parse failures, expiry, late closed-page result ignored, recovery and zero Home requests");
}

int main(int argc,char**argv) {
 assert(argc==5 || argc==7);
 if(argc==7){review_width=atoi(argv[5]);review_height=atoi(argv[6]);}
 assert(review_width>=480 && review_width<=1920 && review_height>=480 && review_height<=1920);
 pixels=calloc((size_t)review_width*review_height,4);assert(pixels);lv_init();
 lv_group_set_default(lv_group_create()); /* Match the A133 evdev keypad group. */
 settings.locale_mode=strcmp(argv[2],"zh")==0?SYS_LOCALE_ZH_CN:SYS_LOCALE_EN_US;
 settings.appearance_mode=strcmp(argv[3],"light")==0?SYS_APPEARANCE_LIGHT:SYS_APPEARANCE_DARK;
 lv_display_t*d=lv_display_create(review_width,review_height);lv_display_set_color_format(d,LV_COLOR_FORMAT_XRGB8888);
 static uint8_t buf[1920*100*4];lv_display_set_buffers(d,buf,NULL,sizeof(buf),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(d,flush);
 if(!desktop_hdmi_supported())settings.hdmi_enabled=true; /* persisted legacy ON must not enable unsupported hardware */
 char widget_fixture_dir[]="/tmp/mimi-ui075-render-XXXXXX";assert(mkdtemp(widget_fixture_dir));
 snprintf(review_layout_dir,sizeof(review_layout_dir),"%s",widget_fixture_dir);atexit(cleanup_review_layout);setenv("TZ","Asia/Shanghai",1);tzset();
 char widget_fixture_path[256];snprintf(widget_fixture_path,sizeof(widget_fixture_path),"%s/settings",widget_fixture_dir);setenv("LV_SETTINGS_FILE",widget_fixture_path,1);
 ui_init();
 /* Deterministic visual fixture, not observed target telemetry. */
 ui_clock_set_sensor(true,30,36);
 cloud_status_t review_cloud={.state=CLOUD_STATE_CONNECTED};emit(TOPIC_CLOUD_STATUS,&review_cloud,sizeof(review_cloud));
 cloud_pairing_status_v1_t review_owner={.version=1,.bytes=sizeof(review_owner),.ownership=CLOUD_PAIRING_BOUND};emit(TOPIC_CLOUD_PAIRING_STATUS,&review_owner,sizeof(review_owner));
 hdmi_preview_status_t review_hdmi={0};emit(TOPIC_HDMI_PREVIEW_STATUS,&review_hdmi,sizeof(review_hdmi));

 if(!desktop_hdmi_supported()){
  pump();int before=saves;
  assert(find_label(lv_scr_act(),ui_tr(UI_STR_HDMI_UNSUPPORTED)));
  assert(!find_label(lv_scr_act(),ui_tr(UI_STR_READY)));
  assert(lv_obj_has_state(hdmi_switch_obj,LV_STATE_DISABLED));
  assert(!lv_obj_has_state(hdmi_switch_obj,LV_STATE_CHECKED));
  lv_obj_send_event(lv_obj_get_parent(hdmi_switch_obj),LV_EVENT_CLICKED,NULL);
  lv_obj_add_state(hdmi_switch_obj,LV_STATE_CHECKED);
  lv_obj_send_event(hdmi_switch_obj,LV_EVENT_VALUE_CHANGED,NULL);
  hdmi_preview_status_t status={.enabled=true,.active=true,.signal_locked=true};
  lv_obj_t*home=lv_scr_act();hdmi_ui_handle_status(&status);pump();
  assert(lv_scr_act()==home && !lv_obj_has_state(hdmi_switch_obj,LV_STATE_CHECKED));
  assert(saves==before && settings.hdmi_enabled && hdmi_mutations==0 && hdmi_control_connections==0);
  puts("PASS unavailable HDMI: persisted ON, direct row/switch event and unsolicited active status cannot activate, save, or connect");
 }
#ifdef REVIEW_REAL_APP_MANAGER
 int refresh_before=pairing_refreshes;
 app_manager_open(APP_ID_SETTING);pump();pump();
 lv_obj_t *pair_entry=find_label(lv_scr_act(),ui_tr(UI_STR_SETTINGS_100ASK_MANAGE));assert(pair_entry);
 lv_obj_scroll_to_view_recursive(lv_obj_get_parent(pair_entry),LV_ANIM_OFF);pump();
 tap_widget(lv_obj_get_parent(pair_entry));pump();pump();
 assert(find_label(lv_scr_act(),ui_i18n_is_zh()?"刷新绑定码":"Refresh code"));
 assert(pairing_refreshes==refresh_before);
 app_manager_back_home();pump();pump();
 puts("PASS real Settings to pairing navigation: read status only, no automatic refresh");
 test_home_navigation();
 test_responsive_home();
 test_launcher_rounds();
 lv_obj_t *home=lv_screen_active();
 assert(!ui_lockscreen_show());
 app_manager_show_hdmi_preview_screen();pump();
 assert(!ui_lockscreen_is_active() && lv_screen_active()!=home);
 app_manager_hide_hdmi_preview_screen();lv_tick_inc(500);pump();assert(lv_screen_active()==home);
 assert(!ui_lockscreen_show());app_manager_open(APP_ID_SETTING);pump();pump();
 assert(!ui_lockscreen_is_active() && lv_screen_active()!=home);
 app_manager_back_home();lv_tick_inc(500);pump();pump();assert(lv_screen_active()==home);
 assert(!ui_lockscreen_show());ui_rebuild_home();pump();
 app_manager_show_hdmi_preview_screen();pump();app_manager_hide_hdmi_preview_screen();pump();
 assert(lv_screen_active()!=home && find_label(lv_screen_active(),ui_tr(UI_STR_HOME_TALK_MIMI)));
 save(argv[4]);puts("PASS real app manager: locked HDMI entry/return, app navigation, rebuilt Home lifetime");return 0;
#endif
 /* Keep the retired component tests explicit; production does not enable it. */
 if(strcmp(argv[1],"lockscreen")==0){ui_lockscreen_init(lv_screen_active());assert(!ui_lockscreen_show());}
 else if(strcmp(argv[1],"home-next")==0){
  lv_obj_t *label=find_label(lv_screen_active(),ui_tr(UI_STR_APP_SETTINGS));assert(label);
  lv_obj_t *strip=lv_obj_get_parent(lv_obj_get_parent(lv_obj_get_parent(label)));
  lv_obj_update_layout(strip);
  lv_obj_scroll_to_x(strip,lv_obj_get_content_width(strip),LV_ANIM_OFF);pump();
  lv_obj_send_event(strip,LV_EVENT_SCROLL_END,NULL);pump();
  assert(!ui_all_apps_is_visible());
 }
 else if(!strcmp(argv[1],"home-iot-summary")){test_iot_summary();}
 else if(!strcmp(argv[1],"environment") || !strcmp(argv[1],"environment-stale")){test_environment_details(!strcmp(argv[1],"environment-stale"));}
 else if(!strcmp(argv[1],"notifications") || !strcmp(argv[1],"notifications-empty")){test_notice(!strcmp(argv[1],"notifications"));}
 else if(!strcmp(argv[1],"widget-behavior")){test_widget_editor();}
 else if(strcmp(argv[1],"widget-editor")==0 || strcmp(argv[1],"widget-library")==0){
  desktop_widgets_edit();pump();pump();assert(find_label(lv_scr_act(),ui_i18n_is_zh()?"保存布局":"Save layout"));
  if(!strcmp(argv[1],"widget-library")){
   lv_indev_t *pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(pointer,touch_read);touch_state=LV_INDEV_STATE_RELEASED;
   ui075_remove_iot(pointer);ui075_open_library(pointer);lv_indev_delete(pointer);
  }
 }
 else if(strcmp(argv[1],"control-center")==0 || !strcmp(argv[1],"control-behavior")){
  lv_indev_t *pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(pointer,touch_read);touch_state=LV_INDEV_STATE_RELEASED;
  tap_screen(pointer,review_width/2,review_width<1000?84:32);lv_indev_delete(pointer);
  lv_obj_t *title=find_label(lv_layer_top(),ui_tr(UI_STR_CONTROL_CENTER));assert(title && lv_obj_is_visible(title));
  if(review_height<620){
   lv_obj_t *panel=lv_obj_get_parent(title);lv_area_t area;lv_obj_get_coords(panel,&area);
   assert(area.y1>=0 && area.y2<review_height);
   lv_obj_t *close=find_label(panel,ui_i18n_is_zh()?"收起  ∧":"Collapse  ^");assert(close);
   pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(pointer,touch_read);
   touch_x=review_width/2;touch_y=review_height-100;touch_state=LV_INDEV_STATE_PRESSED;lv_indev_read(pointer);pump();
   for(int step=1;step<=10;step++){touch_y=review_height-100-step*16;lv_tick_inc(20);lv_indev_read(pointer);lv_timer_handler();}
   touch_state=LV_INDEV_STATE_RELEASED;lv_indev_read(pointer);lv_tick_inc(350);pump();
   assert(lv_obj_is_visible(panel) && lv_obj_get_scroll_y(panel)>0);
   ui075_tap(lv_obj_get_parent(close),pointer);lv_tick_inc(400);pump();
   assert(!lv_obj_is_visible(title));
   tap_screen(pointer,review_width/2,review_width<1000?84:32);lv_tick_inc(400);pump();
   assert(lv_obj_is_visible(title) && lv_obj_get_scroll_y(panel)==0);lv_indev_delete(pointer);
   puts("PASS small control center: bounded viewport, scroll to collapse, real pointer close/reopen");
  }
  if(!strcmp(argv[1],"control-behavior"))test_center_controls();
  if(!desktop_hdmi_supported()){
   lv_obj_t *caption=find_label(lv_layer_top(),ui_i18n_is_zh()?"HDMI 采集":"HDMI capture");assert(caption);
   lv_obj_t *sw=lv_obj_get_child(lv_obj_get_parent(caption),-1);
   assert(lv_obj_check_type(sw,&lv_switch_class));
   assert(lv_obj_has_state(sw,LV_STATE_DISABLED) && !lv_obj_has_state(sw,LV_STATE_CHECKED));
   int before=saves;lv_obj_add_state(sw,LV_STATE_CHECKED);lv_obj_send_event(sw,LV_EVENT_VALUE_CHANGED,NULL);
   assert(!lv_obj_has_state(sw,LV_STATE_CHECKED) && saves==before && hdmi_mutations==0);
   puts("PASS control center: unsupported HDMI disabled; forced event sends nothing");
  }
 }
 else if(strcmp(argv[1],"desktop-update")==0){
  app_setting.init();
  lv_obj_t *entry=find_label(lv_screen_active(),ui_i18n_is_zh()?"桌面应用更新":"Desktop application update");
  assert(entry);
  lv_obj_send_event(lv_obj_get_parent(entry),LV_EVENT_CLICKED,NULL);pump();
  assert(find_label(lv_layer_top(),ui_i18n_is_zh()?"桌面应用更新":"Desktop application update"));
 } else if(strcmp(argv[1],"settings-pairing")==0){
  int before=pairing_refreshes;
  for(int round=0;round<3;round++){
   app_setting.init();pump();
   assert(pairing_refreshes==before);
   cloud_status_t status={.state=CLOUD_STATE_CONNECTED,.bind_token_error=200};
   emit(TOPIC_CLOUD_STATUS,&status,sizeof(status));pump();
   assert(find_label(lv_scr_act(),ui_tr(UI_STR_SETTINGS_100ASK_MANAGE)));
   assert(!find_label(lv_scr_act(),ui_tr(UI_STR_SETTINGS_100ASK_RETRY)));
   strcpy(status.bind_token,"123456");status.bind_token_error=0;status.bind_token_expires_in=60;
   emit(TOPIC_CLOUD_STATUS,&status,sizeof(status));pump();
   assert(!find_label(lv_scr_act(),"123456"));
   status.state=CLOUD_STATE_UNAVAILABLE;emit(TOPIC_CLOUD_STATUS,&status,sizeof(status));pump();
   lv_obj_t *entry=find_label(lv_scr_act(),ui_tr(UI_STR_SETTINGS_100ASK_MANAGE));assert(entry);
   int opened=app_open_count;tap_widget(lv_obj_get_parent(entry));
   assert(app_open_count==opened+1 && last_app_open==APP_ID_PAIRING && pairing_refreshes==before);
   if(round<2)app_setting.close();
  }
  puts("PASS Settings pairing: three entries, no automatic code request, no raw code/error projection, explicit navigation");
 } else if(strcmp(argv[1],"settings")==0){
  app_setting.init();
  ai_status_t ai={.tuya_schema_known=true,.tuya_schema_supported_dps=2,.tuya_schema_required_dps=12};emit(TOPIC_AI_STATUS,&ai,sizeof(ai));
  cloud_status_t cloud={.state=CLOUD_STATE_UNAVAILABLE};emit(TOPIC_CLOUD_STATUS,&cloud,sizeof(cloud));
 }else if(strncmp(argv[1],"bt",2)==0){
  app_bt.init();
  if(strcmp(argv[1],"bt")) {
   bt_runtime_t runtime={.enabled=true,.discoverable=true,.a2dp_connected=true,.audio_streaming=true,.volume=50};
   snprintf(runtime.adapter_name,sizeof(runtime.adapter_name),"Mimi Living Room Speaker");
   snprintf(runtime.adapter_addr,sizeof(runtime.adapter_addr),"12:34:56:78:9A:BC");
   snprintf(runtime.connected_device_name,sizeof(runtime.connected_device_name),"Living Room Music Player");
   emit(TOPIC_BT_RUNTIME,&runtime,sizeof(runtime));
   bt_avrcp_info_t track={.song_len_ms=240000,.song_pos_ms=62000,.is_playing=true,.player_available=true,.playback_state=BT_PLAYBACK_PLAYING};
   snprintf(track.title,sizeof(track.title),"A Very Long Song Title for an Evening by the Window");
   snprintf(track.artist,sizeof(track.artist),"The Acoustic Ensemble and Friends");
   snprintf(track.album,sizeof(track.album),"Live Sessions from the Living Room");
   emit(TOPIC_BT_AVRCP_INFO,&track,sizeof(track));
   if(!strcmp(argv[1],"bt-details") || !strcmp(argv[1],"bt-rename")) {
    lv_obj_t *details=find_label(lv_scr_act(),ui_tr(UI_STR_BT_DEVICE_MANAGEMENT));assert(details);
    lv_obj_send_event(lv_obj_get_parent(details),LV_EVENT_CLICKED,NULL);pump();
    if(!strcmp(argv[1],"bt-rename")) {
     lv_obj_t *name=find_label(lv_scr_act(),ui_tr(UI_STR_BT_NAME_TAP_EDIT));assert(name);
     lv_obj_send_event(lv_obj_get_parent(name),LV_EVENT_CLICKED,NULL);pump();
    }
   }
  }
 }
 else if(strncmp(argv[1],"video",5)==0){
  app_video.init();
  if(!strcmp(argv[1],"video-library") || !strcmp(argv[1],"video-empty")) {
   lv_obj_t *library=find_label(lv_scr_act(),ui_tr(UI_STR_VIDEO_LIBRARY));assert(library);
   lv_obj_update_layout(lv_scr_act());
   assert(lv_obj_get_height(lv_obj_get_parent(library))>=48);
   lv_obj_send_event(lv_obj_get_parent(library),LV_EVENT_CLICKED,NULL);
   lv_tick_inc(500);pump();
   video_playlist_item_t item={.total_count=-1,.index=0,.width=3840,.height=2160,.fps=30};
   snprintf(item.path,sizeof(item.path),"/mnt/sdcard/A long evening walk through the mountains and countryside.mp4");
   if(strcmp(argv[1],"video-empty")) emit(TOPIC_VIDEO_PLAYLIST,&item,sizeof(item));
   item.total_count=!strcmp(argv[1],"video-empty") ? 0 : 1;
   emit(TOPIC_VIDEO_PLAYLIST,&item,sizeof(item));
   lv_obj_t *refresh=find_label(lv_scr_act(),ui_tr(UI_STR_REFRESH));assert(refresh);
   lv_obj_update_layout(lv_scr_act());
   assert(lv_obj_get_height(lv_obj_get_parent(refresh))>=48);
   assert(!lv_obj_has_flag(lv_obj_get_parent(lv_obj_get_parent(lv_obj_get_parent(lv_obj_get_parent(refresh)))),LV_OBJ_FLAG_HIDDEN));
   puts("PASS video library opens from empty state; refresh has 48px touch target");
  }
 }
 else if(strncmp(argv[1],"ai",2)==0){
  app_ai.init();
  ai_status_t ai={.state=AI_STATE_IDLE,.control_center_running=true,.tuya_bound=true};
  if(!strcmp(argv[1],"ai-pairing")){ai.tuya_bound=false;ai.tuya_bind_qr_pending=true;snprintf(ai.bind_url,sizeof(ai.bind_url),"https://example.invalid/host-test-only");}
  if(!strcmp(argv[1],"ai-offline")) ai.state=AI_STATE_NETWORK_UNAVAILABLE;
  if(!strcmp(argv[1],"ai-speaking")) ai.state=AI_STATE_SPEAKING;
  if(!strcmp(argv[1],"ai-free-chat")){ai.state=AI_STATE_LISTENING;ai.free_chat_active=true;ai.chat_mode=AI_CHAT_MODE_FREE;}
  emit(TOPIC_AI_STATUS,&ai,sizeof(ai));
  ai_text_t text={0};snprintf(text.text,sizeof(text.text),"A long response checks readable wrapping and conversation layout without pretending microphone or speaker hardware was tested.");
  emit(TOPIC_AI_TEXT,&text,sizeof(text));pump();
  if(ai.free_chat_active){assert(ai_free_chat_view_is_open());for(int frame=0;frame<12;frame++)pump();}
 }
 else if(strcmp(argv[1],"user")==0){app_user.init();}
 else if(strcmp(argv[1],"hdmi")==0 || strcmp(argv[1],"hdmi-idle")==0){
  review_hdmi_idle=!strcmp(argv[1],"hdmi-idle");app_hdmi_mcp.init();
  if(!desktop_hdmi_supported()) {
   assert(find_label(lv_scr_act(),ui_tr(UI_STR_HDMI_UNSUPPORTED_HINT)));
   assert(!find_label(lv_scr_act(),ui_tr(UI_STR_STOP)));
   assert(hdmi_control_connections==0 && hdmi_mutations==0);
   puts("PASS unavailable HDMI app: clear reason and no agent connection/control");
  }else if(review_hdmi_idle) {
   assert(find_label(lv_scr_act(),ui_tr(UI_STR_READY)));
   assert(find_label(lv_scr_act(),ui_tr(UI_STR_MCP_EMPTY)));
   lv_obj_t *stop=find_label(lv_scr_act(),ui_tr(UI_STR_STOP));assert(stop);
   assert(lv_obj_has_state(lv_obj_get_parent(stop),LV_STATE_DISABLED));
  }
 }
 else if(strncmp(argv[1],"ota",3)==0){
  app_ota.init();ota_status_t status={.state=OTA_STATE_UPDATE_AVAILABLE};
  snprintf(status.version_local,sizeof(status.version_local),"1.4.2");
  snprintf(status.version_remote,sizeof(status.version_remote),"1.5.0");
  status.update_available=true;
  if(!strcmp(argv[1],"ota-error")){status.state=OTA_STATE_ERROR;status.error_code=11;}
  if(!strcmp(argv[1],"ota-downloading")){status.state=OTA_STATE_DOWNLOADING;status.progress=47;}
  emit(TOPIC_OTA_STATUS,&status,sizeof(status));
 }else if(strcmp(argv[1],"voice-keyboard")==0){
  test_voice_keyboard();
 }else if(strcmp(argv[1],"voice")==0){
  app_voice.init();pump();lv_obj_t*entry=find_textarea(lv_scr_act());assert(entry);
  ai_text_t text={0};strcpy(text.text,"You: 打开设置");emit(TOPIC_AI_TEXT,&text,sizeof(text));assert(!strlen(lv_textarea_get_text(entry)));
  ai_status_t state={.state=AI_STATE_IDLE,.tuya_bound=true,.control_center_running=true};emit(TOPIC_AI_STATUS,&state,sizeof(state));
  lv_obj_t*l=find_label(lv_scr_act(),ui_i18n_is_zh()?"启用本次语音控制":"Enable voice for this session");assert(l);lv_obj_send_event(lv_obj_get_parent(l),LV_EVENT_CLICKED,NULL);lv_tick_inc(250);pump();
  l=find_label(lv_scr_act(),ui_i18n_is_zh()?"开始识别":"Start recognition");assert(l);lv_obj_send_event(lv_obj_get_parent(l),LV_EVENT_CLICKED,NULL);
  state.state=AI_STATE_LISTENING;state.free_chat_active=true;emit(TOPIC_AI_STATUS,&state,sizeof(state));
  strcpy(text.text,"Assistant: 打开设置");emit(TOPIC_AI_TEXT,&text,sizeof(text));assert(!strlen(lv_textarea_get_text(entry)));
  strcpy(text.text,"You: 打开设置");emit(TOPIC_AI_TEXT,&text,sizeof(text));assert(!strlen(lv_textarea_get_text(entry)));
  strcpy(text.text,"You: open settings");int before=app_open_count;emit(TOPIC_AI_TEXT,&text,sizeof(text));pump();assert(!strcmp(lv_textarea_get_text(entry),"open settings"));assert(app_open_count==before);
  l=find_label(lv_scr_act(),ui_i18n_is_zh()?"确认执行":"Confirm action");assert(l);lv_obj_send_event(lv_obj_get_parent(l),LV_EVENT_CLICKED,NULL);assert(app_open_count==before+1);
  lv_obj_send_event(lv_obj_get_parent(l),LV_EVENT_CLICKED,NULL);assert(app_open_count==before+1);
  lv_textarea_set_text(entry,"open settings");l=find_label(lv_scr_act(),ui_i18n_is_zh()?"预览文字指令":"Preview command");assert(l);lv_obj_send_event(lv_obj_get_parent(l),LV_EVENT_CLICKED,NULL);lv_tick_inc(31000);pump();
  l=find_label(lv_scr_act(),ui_i18n_is_zh()?"确认执行":"Confirm action");assert(l);lv_obj_send_event(lv_obj_get_parent(l),LV_EVENT_CLICKED,NULL);assert(app_open_count==before+1);
  /* Deadline must be checked on receipt even before the LVGL timer runs. */
  state.state=AI_STATE_IDLE;state.free_chat_active=false;emit(TOPIC_AI_STATUS,&state,sizeof(state));
  l=find_label(lv_scr_act(),ui_i18n_is_zh()?"开始识别":"Start recognition");lv_obj_send_event(lv_obj_get_parent(l),LV_EVENT_CLICKED,NULL);
  state.state=AI_STATE_LISTENING;state.free_chat_active=true;emit(TOPIC_AI_STATUS,&state,sizeof(state));
  lv_tick_inc(30001);strcpy(text.text,"You: open wifi");emit(TOPIC_AI_TEXT,&text,sizeof(text));assert(!strlen(lv_textarea_get_text(entry)));
  /* A long string must not be truncated into a recognized short command. */
  state.state=AI_STATE_IDLE;state.free_chat_active=false;emit(TOPIC_AI_STATUS,&state,sizeof(state));
  lv_obj_send_event(lv_obj_get_parent(l),LV_EVENT_CLICKED,NULL);
  state.state=AI_STATE_LISTENING;state.free_chat_active=true;emit(TOPIC_AI_STATUS,&state,sizeof(state));
  memset(text.text,' ',sizeof(text.text));memcpy(text.text,"You: open settings",18);strcpy(text.text+180," invalid suffix");
  emit(TOPIC_AI_TEXT,&text,sizeof(text));assert(!strlen(lv_textarea_get_text(entry)));
  /* Provider session closure disarms acceptance immediately. */
  state.state=AI_STATE_IDLE;state.free_chat_active=false;emit(TOPIC_AI_STATUS,&state,sizeof(state));
  lv_obj_send_event(lv_obj_get_parent(l),LV_EVENT_CLICKED,NULL);
  state.state=AI_STATE_LISTENING;state.free_chat_active=true;emit(TOPIC_AI_STATUS,&state,sizeof(state));
  state.state=AI_STATE_IDLE;state.free_chat_active=false;emit(TOPIC_AI_STATUS,&state,sizeof(state));
  strcpy(text.text,"You: go home");emit(TOPIC_AI_TEXT,&text,sizeof(text));assert(!strlen(lv_textarea_get_text(entry)));
  assert(app_open_count==before+1);
  /* Manual editing takes ownership of the text and stops speech input. */
  lv_obj_send_event(lv_obj_get_parent(l),LV_EVENT_CLICKED,NULL);
  state.state=AI_STATE_LISTENING;state.free_chat_active=true;emit(TOPIC_AI_STATUS,&state,sizeof(state));
  lv_textarea_set_text(entry,"open bluetooth");strcpy(text.text,"You: open pairing");
  emit(TOPIC_AI_TEXT,&text,sizeof(text));assert(!strcmp(lv_textarea_get_text(entry),"open bluetooth"));
  assert(find_label(lv_scr_act(),ui_i18n_is_zh()?"已停止语音输入，请预览文字指令":"Voice input stopped. Preview your typed command."));
  puts("PASS voice: cached/duplicate text, assistant text, expired receipt before timer, long transcript, closed session rejected; confirmation single-use");
 }else if(strncmp(argv[1],"iot",3)==0){
  review_iot_connected=strcmp(argv[1],"iot")!=0;
  bool rejected=!strcmp(argv[1],"iot-rejected"),malformed=!strcmp(argv[1],"iot-malformed");
  bool long_modal=!strcmp(argv[1],"iot-long-modal");
  if(long_modal)review_iot_name="TEST living room lamp with a deliberately long descriptive name to verify wrapping and access to confirmation and cancellation on small screens TEST living room lamp with a deliberately long descriptive name to verify wrapping on small screens";
  if(rejected)review_iot_reply="{\"accepted\":false,\"state_confirmed\":false}";
  if(malformed)review_iot_reply="{\"accepted\":true,\"state_confirmed\":false} garbage";
  if(!strcmp(argv[1],"iot-reopen")){
   review_iot_delay_us=300000;app_iot.init();app_iot.close();app_iot.init();pump();
   assert(find_label(lv_scr_act(),ui_i18n_is_zh()?"正在等待上一请求结束…":"Waiting for the previous request to finish…"));
   for(int i=0;i<80;i++){usleep(10000);lv_tick_inc(100);pump();}
   assert(find_label(lv_scr_act(),ui_i18n_is_zh()?"TEST Lamp  ·  在线":"TEST Lamp  ·  Online"));
   puts("PASS IoT reopen: pending request visible; obsolete generation discarded; refreshed devices shown");
   save(argv[4]);return 0;
  }
  app_iot.init();for(int i=0;i<10;i++){usleep(10000);lv_tick_inc(100);pump();}
  if(review_iot_connected){
   char device_label[600];snprintf(device_label,sizeof(device_label),"%s  ·  %s",review_iot_name,ui_i18n_is_zh()?"在线":"Online");
   lv_obj_t*l=long_modal?find_label_prefix(lv_scr_act(),"TEST living room"):find_label(lv_scr_act(),device_label);assert(l);
   tap_widget(lv_obj_get_parent(l));
   for(int i=0;i<10;i++){usleep(10000);lv_tick_inc(100);pump();}
   l=find_label(lv_scr_act(),ui_i18n_is_zh()?"设置此项…":"Set value…");assert(l);
   tap_widget(lv_obj_get_parent(l));pump();
   assert(review_iot_commands==0);
   l=find_label(lv_layer_top(),ui_i18n_is_zh()?"取消":"Cancel");assert(l);
   tap_widget(lv_obj_get_parent(l));pump();assert(review_iot_commands==0);
   l=find_label(lv_scr_act(),ui_i18n_is_zh()?"设置此项…":"Set value…");assert(l);tap_widget(lv_obj_get_parent(l));pump();
   l=find_label(lv_layer_top(),ui_i18n_is_zh()?"确认发送":"Send command");assert(l);
   if(long_modal){
    lv_obj_t*m=lv_obj_get_parent(lv_obj_get_parent(l));lv_area_t a;lv_obj_get_coords(m,&a);assert(a.x1>=0 && a.y1>=0 && a.x2<review_width && a.y2<review_height);
    lv_obj_scroll_to_view_recursive(lv_obj_get_parent(l),LV_ANIM_OFF);pump();
    save(argv[4]);puts("PASS long device name: wrapped list, pointer selection/cancel, bounded scrollable confirmation");return 0;
   }
   tap_widget(lv_obj_get_parent(l));
   for(int i=0;i<10;i++){usleep(10000);lv_tick_inc(100);pump();}
   assert(review_iot_commands==1);
   assert(mimi_iot_summary_get().state!=MIMI_IOT_SUMMARY_READY);
   const char *accepted=ui_i18n_is_zh()?"指令已受理，尚未确认设备状态。请刷新核对结果。":"Command accepted; device state is not confirmed. Refresh to check the result.";
   if(rejected || malformed){assert(!find_label(lv_scr_act(),ui_i18n_is_zh()?"设置此项…":"Set value…"));assert(!find_label(lv_scr_act(),accepted));assert(find_label(lv_scr_act(),ui_i18n_is_zh()?"未确认指令受理结果。请先刷新设备状态，避免重复操作。":"Command acceptance is unconfirmed. Refresh device state before retrying."));}
   else assert(find_label(lv_scr_act(),accepted));
   puts("PASS IoT host fixture: explicit confirmation, cancel sends nothing, one command, no false state confirmation");
  }
 }else if(!strcmp(argv[1],"pairing-bound") || !strcmp(argv[1],"pairing-unbound") || !strcmp(argv[1],"pairing-unknown") || !strcmp(argv[1],"pairing-requery")){
  app_pairing.init();cloud_status_t status={.state=CLOUD_STATE_CONNECTED};
  emit(TOPIC_CLOUD_STATUS,&status,sizeof(status));
  cloud_pairing_status_v1_t pair={.version=1,.bytes=sizeof(pair)};
  if(!strcmp(argv[1],"pairing-bound") || !strcmp(argv[1],"pairing-requery"))pair.ownership=CLOUD_PAIRING_BOUND;
  else if(!strcmp(argv[1],"pairing-unbound")){pair.ownership=CLOUD_PAIRING_UNBOUND;pair.expires_in=598;strcpy(pair.code,"123456");}
  emit(TOPIC_CLOUD_PAIRING_STATUS,&pair,sizeof(pair));pump();
  lv_obj_t*refresh=find_label(lv_scr_act(),pair.ownership==CLOUD_PAIRING_BOUND ? (ui_i18n_is_zh()?"检查绑定状态":"Check binding status") : (ui_i18n_is_zh()?"刷新绑定码":"Refresh code"));assert(refresh);
  if(pair.ownership==CLOUD_PAIRING_BOUND){
   assert(find_label(lv_scr_act(),ui_i18n_is_zh()?"设备已绑定账号，无需绑定码。账号归属和解绑请在 100ask.net 查看。":"Device already linked. Check ownership or unlink at 100ask.net."));
   assert(!lv_obj_has_state(lv_obj_get_parent(refresh),LV_STATE_DISABLED));
   assert(!lv_obj_has_flag(lv_obj_get_parent(refresh),LV_OBJ_FLAG_HIDDEN));
   assert(!find_label(lv_scr_act(),"123456"));
  }else if(pair.ownership==CLOUD_PAIRING_UNBOUND)assert(find_label(lv_scr_act(),"123456"));
  else assert(find_label(lv_scr_act(),ui_i18n_is_zh()?"账号归属尚未确认。可刷新查询，或在 100ask.net 查看设备绑定。":"Account ownership is unconfirmed. Refresh to check, or review device binding at 100ask.net."));
  unsigned expected_refreshes=0;
  if(!strcmp(argv[1],"pairing-requery")){
   assert(pairing_refreshes==0);
   tap_widget(lv_obj_get_parent(refresh));assert(pairing_refreshes==1);
   assert(!find_label(lv_scr_act(),"123456"));
   status.bind_token_refreshing=false;status.bind_token_error=0;
   emit(TOPIC_CLOUD_STATUS,&status,sizeof(status));
   pair.ownership=CLOUD_PAIRING_UNBOUND;pair.expires_in=598;strcpy(pair.code,"123456");
   emit(TOPIC_CLOUD_PAIRING_STATUS,&pair,sizeof(pair));pump();
   assert(find_label(lv_scr_act(),"123456"));
   assert(find_label(lv_scr_act(),ui_i18n_is_zh()?"刷新绑定码":"Refresh code"));
   assert(!lv_obj_has_state(lv_obj_get_parent(refresh),LV_STATE_DISABLED));
   for(unsigned settle=0;settle<6;settle++)pump();
   expected_refreshes=1;
  }
  pair.version=99;pair.ownership=CLOUD_PAIRING_UNBOUND;pair.expires_in=599;strcpy(pair.code,"654321");
  emit(TOPIC_CLOUD_PAIRING_STATUS,&pair,sizeof(pair));assert(!find_label(lv_scr_act(),"654321"));
  assert(pairing_refreshes==expected_refreshes);
  puts("PASS PAIR071 real UI: authoritative ownership, strict optional version, no automatic refresh");
 }else if(strcmp(argv[1],"pairing")==0){
  app_pairing.init();assert(find_label(lv_scr_act(),ui_i18n_is_zh()?"— — — — — —":"- - - - - -"));cloud_status_t status={.state=CLOUD_STATE_CONNECTED,.bind_token_expires_in=60};
  strcpy(status.bind_token,"123456");emit(TOPIC_CLOUD_STATUS,&status,sizeof(status));pump();
  assert(find_label(lv_scr_act(),"123456"));
  lv_tick_inc(61000);pump();assert(!find_label(lv_scr_act(),"123456"));
  strcpy(status.bind_token,"1234a6");status.bind_token_expires_in=60;
  emit(TOPIC_CLOUD_STATUS,&status,sizeof(status));assert(!find_label(lv_scr_act(),"1234a6"));
  strcpy(status.bind_token,"123456");emit(TOPIC_CLOUD_STATUS,&status,sizeof(status));pump();
  lv_obj_t*refresh=find_label(lv_scr_act(),ui_i18n_is_zh()?"刷新绑定码":"Refresh code");assert(refresh);
  tap_widget(lv_obj_get_parent(refresh));assert(pairing_refreshes==1);assert(!find_label(lv_scr_act(),"123456"));
  emit(TOPIC_CLOUD_STATUS,&status,sizeof(status));pump();
  status.bind_token[0]='\0';status.bind_token_expires_in=0;status.bind_token_error=200;
  emit(TOPIC_CLOUD_STATUS,&status,sizeof(status));pump();
  assert(find_label(lv_scr_act(),ui_i18n_is_zh()?"暂未取得可用绑定码。若设备已绑定，可继续使用；账号归属请在 100ask.net 查看。":"No usable pairing code was returned. If already linked, continue using the device. Check account ownership at 100ask.net."));
  assert(!find_label(lv_scr_act(),"123456"));
  assert(pairing_refreshes==1); /* no automatic retry, reprovision or unlink */
  puts("PASS pairing: valid/expired/malformed code and ambiguous HTTP 200; no claim of lost ownership or server fault");
 }else if(strcmp(argv[1],"apps")==0){ui_all_apps_show();pump();lv_tick_inc(250);pump();}
 else if(strcmp(argv[1],"wifi")==0 || strcmp(argv[1],"password")==0){
  app_wifi.init();wifi_spinner_delete();
  wifi_ap_t ap={.ssid="100ASK-Lab",.encrypted=true};
  lv_obj_t*item=create_list_item(wifi_list,&ap);
  ap.ssid="Long-Network-Name-For-Layout-Test";create_list_item(wifi_list,&ap);
  ap.ssid="Workshop-IoT";ap.encrypted=false;create_list_item(wifi_list,&ap);
  if(strcmp(argv[1],"password")==0)lv_obj_send_event(item,LV_EVENT_CLICKED,NULL);
 }
 save(argv[4]);inspect(lv_scr_act());
 if(!strcmp(argv[1],"ai-free-chat")){
  int before=ai_exit_count;
  lv_obj_send_event(lv_obj_get_child(lv_layer_top(),-1),LV_EVENT_DOUBLE_CLICKED,NULL);
  assert(ai_exit_count==before+1);
  ai_status_t idle={.state=AI_STATE_IDLE,.control_center_running=true,.tuya_bound=true,.chat_mode=AI_CHAT_MODE_WAKEUP};
  emit(TOPIC_AI_STATUS,&idle,sizeof(idle));pump();assert(!ai_free_chat_view_is_open());
  puts("PASS real free-chat view: assets rendered, exit command, backend acknowledgement closes overlay");
 }
 if(!strcmp(argv[1],"bt-rename")) {
  lv_obj_t *cancel=find_label(lv_layer_top(),ui_tr(UI_STR_CANCEL));assert(cancel);
  lv_obj_t *dialog=lv_obj_get_parent(lv_obj_get_parent(lv_obj_get_parent(cancel)));
  lv_obj_t *modal=lv_obj_get_parent(dialog);
  lv_obj_t *keyboard=lv_obj_get_child(modal,-1);assert(lv_obj_check_type(keyboard,&lv_keyboard_class));
  lv_area_t da,ka;lv_obj_get_coords(dialog,&da);lv_obj_get_coords(keyboard,&ka);
  assert(da.y1>=12 && da.y2<ka.y1 && ka.y2<review_height);
  assert(lv_obj_get_height(lv_obj_get_parent(cancel))>=48);
  lv_font_glyph_dsc_t glyph;
  const lv_font_t *key_font=lv_obj_get_style_text_font(keyboard,LV_PART_ITEMS);
  assert(lv_font_get_glyph_dsc(key_font,&glyph,0xf55a,0) && !glyph.is_placeholder);
  int before=saves;lv_obj_send_event(lv_obj_get_parent(cancel),LV_EVENT_CLICKED,NULL);
  assert(!find_label(lv_layer_top(),ui_tr(UI_STR_BT_RENAME)) && saves==before);
  puts("PASS Bluetooth rename: dialog above keyboard, readable keys, 48px actions, cancel without saving");
 }
 if(!strcmp(argv[1],"desktop-update")){
  lv_obj_t *close=find_label(lv_layer_top(),ui_i18n_is_zh()?"关闭":"Close");assert(close);
  lv_obj_send_event(lv_obj_get_parent(close),LV_EVENT_CLICKED,NULL);pump();
  assert(!find_label(lv_layer_top(),ui_i18n_is_zh()?"桌面应用更新":"Desktop application update"));
  puts("PASS desktop update: dedicated settings entry, close stays on Settings");
 }
 if(!strcmp(argv[1],"ota-downloading")){
  lv_obj_t *cancel=find_label(lv_scr_act(),ui_tr(UI_STR_CANCEL));assert(cancel);
  int before=ota_commands;lv_obj_send_event(lv_obj_get_parent(cancel),LV_EVENT_CLICKED,NULL);
  assert(ota_commands==before+1 && last_ota_command==OTA_CMD_CANCEL);
  ota_status_t installing={.state=OTA_STATE_APPLYING,.progress=10};
  emit(TOPIC_OTA_STATUS,&installing,sizeof(installing));pump();
  lv_obj_t *check=find_label(lv_scr_act(),ui_tr(UI_STR_OTA_CHECK));assert(check);
  assert(lv_obj_has_state(lv_obj_get_parent(check),LV_STATE_DISABLED));
  puts("PASS OTA: download cancel routes correctly; installation disables cancellation");
 }
 if(!strcmp(argv[1],"settings"))test_settings();
 if(!strcmp(argv[1],"password"))test_password();
 if(!strcmp(argv[1],"lockscreen"))test_lockscreen();
 if(!strcmp(argv[1],"home")){
  lv_obj_t *home=lv_screen_active();lv_tick_inc(UI_LOCKSCREEN_IDLE_MS+1600);pump();pump();
  assert(lv_screen_active()==home && !ui_lockscreen_is_active());
  puts("PASS Home stays visible while idle; cancelled lockscreen has no automatic entry");
 }
 return 0;
}
