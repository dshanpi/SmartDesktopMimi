#include "desktop_status_bar.h"
#include "ui_helpers.h"
#include "theme/theme.h"
#include "desktop_home_tokens.h"
#include "ui_i18n.h"
#include "desktop_capabilities.h"
#include "desktop_widgets.h"
#include "../system/backlight_control.h"
#include "../system/volume_control.h"
#include "../system/app_manager.h"
#include "../system/settings.h"
#include "../system/backend_types.h"
#include "../middleware/middleware.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

LV_IMAGE_DECLARE(wireless_network_wifi);
LV_IMAGE_DECLARE(cloud);

/* 顶栏布局尺寸 */
#define HDMI_MODE_ROW_WIDTH (HOME_SCREEN_W >= 1000 ? 252 : 200)
#define HDMI_MODE_ROW_HEIGHT 36
#define HDMI_MODE_LABEL_WIDTH (HOME_SCREEN_W >= 1000 ? 178 : 128)
#define TOP_STATUS_GROUP_WIDTH (HOME_SCREEN_W >= 1000 ? 232 : 210)
#define TOP_POWER_LABEL_WIDTH 300
#define CONTROL_CENTER_WIDTH (HOME_SCREEN_W >= 1000 ? 846 : HOME_SCREEN_W - 32)
#define CONTROL_CENTER_HEIGHT LV_MIN(572, HOME_SCREEN_H - 48)
#define CONTROL_CENTER_CARD_WIDTH 216
#define CONTROL_CENTER_CARD_HEIGHT 184

/* =======================
 * 全局变量（ui_helpers.c 的回调通过 extern 引用）
 * ======================= */
lv_obj_t *wifi_icon_obj = NULL;
lv_obj_t *hdmi_switch_obj = NULL;
lv_obj_t *cloud_icon_obj = NULL;
lv_obj_t *cloud_status_dot_obj = NULL;
lv_obj_t *cloud_progress_track_obj = NULL; /* OTA download track under cloud icon */
lv_obj_t *cloud_progress_fill_obj = NULL;
bool wifi_connected = false;
char wifi_connected_ssid[33] = {0};
bool wifi_runtime_enabled = true;
bool wifi_runtime_scanning = false;
bool wifi_has_ip = false;
bool wifi_startup_scan_decided = false;
lv_timer_t *wifi_startup_scan_timer = NULL;
lv_obj_t *power_label = NULL;   /* INA219 整机功耗/电流/电压（顶栏中间） */

/* HDMI 显示模式开关状态（顶栏 + HDMI 全屏覆盖层） */
static bool hdmi_ignore_event = false;
static bool hdmi_toggle_pending = false;
static bool hdmi_toggle_target = false;
static lv_timer_t *hdmi_pending_timer = NULL;
static lv_obj_t *hdmi_overlay_switch = NULL;
static lv_obj_t *control_center_overlay = NULL;
static lv_obj_t *control_center_panel = NULL;
static lv_obj_t *control_center_wifi_detail = NULL;

static lv_obj_t *control_center_hdmi_switch = NULL;
static bool control_center_visible = false;
static lv_obj_t *control_center_chevron = NULL;

/* 前置声明 */
static void on_hdmi_switch_changed(lv_event_t *e);

static void on_control_center_hdmi_changed(lv_event_t *e);
static void hdmi_switch_sync(bool enabled);
static void hdmi_send_enable(bool on);
static void hdmi_pending_timeout_cb(lv_timer_t *timer);
static void hdmi_clear_pending(void);
static void control_center_show(void);
static void control_center_hide(bool animate);

typedef enum {
    CONTROL_CENTER_ACTION_WIFI = 1,
    CONTROL_CENTER_ACTION_USER_APPS,
    CONTROL_CENTER_ACTION_SETTINGS,
    CONTROL_CENTER_ACTION_PAIRING,
} control_center_action_t;

static void control_center_anim_y(void *obj, int32_t y)
{
    lv_obj_set_y((lv_obj_t *)obj, y);
}

static void control_center_hide_ready(lv_anim_t *anim)
{
    (void)anim;
    if (control_center_overlay && lv_obj_is_valid(control_center_overlay)) {
        lv_obj_add_flag(control_center_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

static void control_center_hide(bool animate)
{
    if (!control_center_overlay || !control_center_panel || !control_center_visible) return;
    control_center_visible = false;
    if (control_center_chevron && lv_obj_is_valid(control_center_chevron))
        lv_label_set_text(control_center_chevron, LV_SYMBOL_DOWN);
    lv_anim_delete(control_center_panel, control_center_anim_y);

    if (!animate) {
        lv_obj_set_y(control_center_panel, -CONTROL_CENTER_HEIGHT - 24);
        lv_obj_add_flag(control_center_overlay, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, control_center_panel);
    lv_anim_set_values(&anim, lv_obj_get_y(control_center_panel),
                       -CONTROL_CENTER_HEIGHT - 24);
    lv_anim_set_time(&anim, 220);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in);
    lv_anim_set_exec_cb(&anim, control_center_anim_y);
    lv_anim_set_ready_cb(&anim, control_center_hide_ready);
    lv_anim_start(&anim);
}

static void control_center_overlay_event(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_CLICKED &&
        lv_event_get_target(event) == lv_event_get_current_target(event)) {
        control_center_hide(true);
    }
}

static void control_center_panel_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_GESTURE) return;
    if (CONTROL_CENTER_HEIGHT < 572) return;
    lv_indev_t *indev = lv_indev_active();
    if (indev && lv_indev_get_gesture_dir(indev) == LV_DIR_TOP) {
        control_center_hide(true);
    }
}

static void control_center_action_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    control_center_action_t action =
        (control_center_action_t)(intptr_t)lv_event_get_user_data(event);

    control_center_hide(false);
    if (action == CONTROL_CENTER_ACTION_WIFI) {
        app_manager_open(APP_ID_WIFI);
    } else if (action == CONTROL_CENTER_ACTION_USER_APPS) {
        app_manager_open(APP_ID_USER_APPS);
    } else if (action == CONTROL_CENTER_ACTION_PAIRING) {
        app_manager_open(APP_ID_PAIRING);
    } else if (action == CONTROL_CENTER_ACTION_SETTINGS) {
        app_manager_open(APP_ID_SETTING);
    }
}

static lv_obj_t *center_connection,*center_hdmi_detail,*center_notice,*center_error;
static lv_obj_t *brightness_slider,*volume_slider,*brightness_value,*volume_value,*mute_switch,*mute_detail;
static int volume_before_mute=40;
static bool hdmi_actual,hdmi_known;
static bool notice_known;
static ota_state_t notice_state;
static int notice_progress;
static lv_obj_t *notice_row;
void ui_statusbar_ota_notice(const ota_status_t *status)
{
 notice_known = status && status->state >= OTA_STATE_IDLE && status->state <= OTA_STATE_ERROR;
 if(!notice_known) return;
 notice_state = status->state;
 notice_progress = LV_CLAMP(0,status->progress,100);
 /* Never copy version strings, provider errors or cloud payloads into a notification. */
}
static bool notice_active(void)
{
 return notice_known && notice_state != OTA_STATE_IDLE && notice_state != OTA_STATE_UP_TO_DATE;
}
static void notice_open(lv_event_t *e)
{
 (void)e;
 if(!notice_active()) return;
 control_center_hide(false);
 app_manager_open(APP_ID_OTA);
}
static const char *tr(const char *zh,const char *en){return ui_i18n_is_zh()?zh:en;}
static lv_obj_t *cc_label(lv_obj_t *p,const char *text,int x,int y,const lv_font_t *font,lv_color_t color)
{
 lv_obj_t *l=lv_label_create(p);lv_label_set_text(l,text);lv_obj_set_pos(l,x,y);
 lv_obj_set_style_text_font(l,font,0);lv_obj_set_style_text_color(l,color,0);lv_obj_clear_flag(l,LV_OBJ_FLAG_CLICKABLE);return l;
}
static lv_obj_t *cc_surface(lv_obj_t *p,int x,int y,int w,int h)
{
 lv_obj_t *o=lv_obj_create(p);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
 lv_obj_set_style_radius(o,18,0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_set_style_bg_color(o,HOME_SURFACE_RAISED,0);
 lv_obj_set_style_border_color(o,HOME_BORDER,0);lv_obj_set_style_border_width(o,1,0);lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE);return o;
}
static void cc_sun(lv_obj_t *parent)
{
 lv_obj_t *circle=cc_surface(parent,29,30,16,16);lv_obj_set_style_radius(circle,LV_RADIUS_CIRCLE,0);lv_obj_set_style_bg_color(circle,HOME_CORAL,0);lv_obj_set_style_border_width(circle,0,0);
 static const lv_point_precise_t rays[8][2]={{{37,16},{37,23}},{{37,53},{37,60}},{{15,38},{22,38}},{{52,38},{59,38}},{{21,22},{26,27}},{{48,49},{53,54}},{{21,54},{26,49}},{{48,27},{53,22}}};
 for(unsigned i=0;i<8;i++){lv_obj_t *line=lv_line_create(parent);lv_line_set_points(line,rays[i],2);lv_obj_set_style_line_width(line,3,0);lv_obj_set_style_line_color(line,HOME_CORAL,0);lv_obj_set_style_line_rounded(line,true,0);}
}
static lv_obj_t *cc_tile(int x,int y,int w,const char *name,const char *symbol,lv_color_t accent,control_center_action_t action,lv_obj_t **detail)
{
 lv_obj_t *card=cc_surface(control_center_panel,x,y,w,100);
 lv_obj_set_style_bg_color(card,lv_color_mix(HOME_SURFACE,accent,246),0);
 lv_obj_set_style_border_color(card,lv_color_mix(HOME_SURFACE,accent,219),0);
 lv_obj_t *icon=cc_surface(card,22,18,64,64);lv_obj_set_style_bg_color(icon,lv_color_mix(HOME_SURFACE,accent,220),0);lv_obj_set_style_border_width(icon,0,0);
 lv_obj_t *glyph=cc_label(icon,symbol,0,0,&lv_font_montserrat_32,accent);lv_obj_center(glyph);
 if(!strcmp(symbol,LV_SYMBOL_REFRESH)){lv_obj_add_flag(glyph,LV_OBJ_FLAG_HIDDEN);lv_obj_t *image=lv_image_create(icon);lv_image_set_src(image,&cloud);lv_obj_set_size(image,54,54);lv_image_set_inner_align(image,LV_IMAGE_ALIGN_CONTAIN);lv_obj_set_style_image_recolor(image,accent,0);lv_obj_set_style_image_recolor_opa(image,LV_OPA_COVER,0);lv_obj_center(image);}
 if(!strcmp(symbol,LV_SYMBOL_VIDEO)){lv_obj_add_flag(glyph,LV_OBJ_FLAG_HIDDEN);lv_obj_t *screen=cc_surface(icon,14,15,36,26);lv_obj_set_style_bg_opa(screen,LV_OPA_TRANSP,0);lv_obj_set_style_radius(screen,3,0);lv_obj_set_style_border_width(screen,3,0);lv_obj_set_style_border_color(screen,accent,0);lv_obj_t *stand=cc_surface(icon,24,46,18,3);lv_obj_set_style_bg_color(stand,accent,0);lv_obj_set_style_border_width(stand,0,0);}

 cc_label(card,name,116,23,ui_font_h3(),HOME_TEXT);
 *detail=cc_label(card,"--",116,56,ui_font_body_md(),HOME_TEXT_MUTED);lv_obj_set_width(*detail,w-178);lv_label_set_long_mode(*detail,LV_LABEL_LONG_DOT);
 if(action){lv_obj_add_flag(card,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(card,control_center_action_event,LV_EVENT_CLICKED,(void*)(intptr_t)action);cc_label(card,LV_SYMBOL_RIGHT,w-31,35,&lv_font_montserrat_16,HOME_TEXT_MUTED);}
 return card;
}
static void cc_controls_read(void)
{
 int value;
 if(sys_backlight_get_percent(&value)==0){lv_slider_set_value(brightness_slider,value,LV_ANIM_OFF);lv_label_set_text_fmt(brightness_value,"%d%%",value);lv_obj_clear_state(brightness_slider,LV_STATE_DISABLED);}
 else{lv_label_set_text(brightness_value,tr("不可用","Unavailable"));lv_obj_add_state(brightness_slider,LV_STATE_DISABLED);}
 if(sys_volume_get_percent(&value)==0){lv_slider_set_value(volume_slider,value,LV_ANIM_OFF);lv_label_set_text_fmt(volume_value,"%d%%",value);lv_obj_clear_state(volume_slider,LV_STATE_DISABLED);lv_obj_clear_state(mute_switch,LV_STATE_DISABLED);
  if(value>0){volume_before_mute=value;lv_obj_clear_state(mute_switch,LV_STATE_CHECKED);}else lv_obj_add_state(mute_switch,LV_STATE_CHECKED);
  lv_label_set_text(mute_detail,value?tr("未开启","Off"):tr("已静音","Muted"));
 }else{lv_label_set_text(volume_value,tr("不可用","Unavailable"));lv_label_set_text(mute_detail,tr("不可用","Unavailable"));lv_obj_add_state(volume_slider,LV_STATE_DISABLED);lv_obj_add_state(mute_switch,LV_STATE_DISABLED);}
}
static void cc_apply_control(bool brightness,int requested)
{
 if(brightness && requested<20)requested=20;
 int actual=-1,rc=brightness?sys_backlight_set_percent(requested):sys_volume_set_percent(requested);
 if(rc==0)rc=brightness?sys_backlight_get_percent(&actual):sys_volume_get_percent(&actual);
 if(rc || actual<0 || actual>100){lv_label_set_text(center_error,tr("设置失败，请重试","Setting failed; please retry"));cc_controls_read();return;}
 if(brightness)sys_settings_get()->brightness=actual;else sys_settings_get()->volume=actual;
 int saved_rc=sys_settings_save_checked();
 lv_label_set_text(center_error,saved_rc?tr("已应用，保存失败；重启后可能恢复旧值","Applied, but saving failed; may reset after restart"):"");
 cc_controls_read();
}
static void cc_slider_event(lv_event_t *e)
{
 bool brightness=(bool)(intptr_t)lv_event_get_user_data(e);lv_obj_t *slider=lv_event_get_target(e);
 if(lv_obj_has_state(slider,LV_STATE_DISABLED))return;
 if(lv_event_get_code(e)==LV_EVENT_VALUE_CHANGED)lv_label_set_text_fmt(brightness?brightness_value:volume_value,"%d%%",(int)lv_slider_get_value(slider));
 else if(lv_event_get_code(e)==LV_EVENT_RELEASED)cc_apply_control(brightness,lv_slider_get_value(slider));
}
static void cc_mute_event(lv_event_t *e)
{
 if(lv_obj_has_state(mute_switch,LV_STATE_DISABLED))return;
 cc_apply_control(false,lv_obj_has_state(lv_event_get_target(e),LV_STATE_CHECKED)?0:volume_before_mute);
}
static void cc_close(lv_event_t *e){(void)e;control_center_hide(true);}
static void notice_refresh(void)
{
 if(!center_notice || !notice_row) return;
 if(!notice_active()) {
  lv_label_set_text(center_notice,tr("通知    暂无本地通知","Notifications    No local notifications"));
  lv_obj_add_state(notice_row,LV_STATE_DISABLED);
  return;
 }
 lv_obj_clear_state(notice_row,LV_STATE_DISABLED);
 const char *phase = notice_state == OTA_STATE_CHECKING ? tr("检查中","Checking")
  : notice_state == OTA_STATE_UPDATE_AVAILABLE ? tr("发现可用更新","Update available")
  : notice_state == OTA_STATE_DOWNLOADING ? tr("下载中","Downloading")
  : notice_state == OTA_STATE_DOWNLOAD_DONE ? tr("下载完成，等待安装","Downloaded; awaiting installation")
  : notice_state == OTA_STATE_APPLYING ? tr("安装中","Installing")
  : notice_state == OTA_STATE_REBOOTING ? tr("等待重启","Awaiting restart")
  : notice_state == OTA_STATE_COMMITTING ? tr("确认新系统","Confirming new system")
  : tr("更新失败，点此查看","Update failed; tap for details");
 if(notice_state == OTA_STATE_DOWNLOADING || notice_state == OTA_STATE_APPLYING)
  lv_label_set_text_fmt(center_notice,tr("系统更新 · %s %d%%","System update · %s %d%%"),phase,notice_progress);
 else lv_label_set_text_fmt(center_notice,tr("系统更新 · %s","System update · %s"),phase);
}
static void cc_status(void)
{
 if(!control_center_visible)return;
 lv_label_set_text(control_center_wifi_detail,wifi_connected?tr("已连接","Connected"):wifi_runtime_enabled?tr("未连接","Disconnected"):ui_tr(UI_STR_WIFI_OFF));
 notice_refresh();
 lv_label_set_text(center_connection,desktop_widgets_connection_text());
 lv_label_set_text(center_hdmi_detail,!desktop_hdmi_supported()?ui_tr(UI_STR_HDMI_UNSUPPORTED):hdmi_toggle_pending?tr("正在切换…","Switching…"):!hdmi_known?tr("状态待确认","Status unavailable"):hdmi_actual?tr("采集已开启","Capture enabled"):tr("采集已关闭","Capture disabled"));
}
static void cc_status_tick(lv_timer_t *t){(void)t;cc_status();}
static void control_center_build(void)
{
 if(control_center_overlay && lv_obj_is_valid(control_center_overlay))return;
 control_center_overlay=cc_surface(lv_layer_top(),0,0,HOME_SCREEN_W,HOME_SCREEN_H);
 lv_obj_set_style_bg_color(control_center_overlay,lv_color_black(),0);lv_obj_set_style_bg_opa(control_center_overlay,LV_OPA_30,0);lv_obj_set_style_radius(control_center_overlay,0,0);
 lv_obj_add_flag(control_center_overlay,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(control_center_overlay,control_center_overlay_event,LV_EVENT_CLICKED,NULL);
 control_center_panel=cc_surface(control_center_overlay,(HOME_SCREEN_W-CONTROL_CENTER_WIDTH)/2,87,CONTROL_CENTER_WIDTH,CONTROL_CENTER_HEIGHT);
 lv_obj_set_style_bg_color(control_center_panel,HOME_SURFACE,0);lv_obj_set_style_radius(control_center_panel,26,0);lv_obj_add_flag(control_center_panel,LV_OBJ_FLAG_CLICKABLE);
 lv_obj_add_event_cb(control_center_panel,control_center_panel_event,LV_EVENT_GESTURE,NULL);
 if(CONTROL_CENTER_HEIGHT < 572){lv_obj_add_flag(control_center_panel,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(control_center_panel,LV_DIR_VER);}
 int w=CONTROL_CENTER_WIDTH,tw=(w-60)/2;
 cc_label(control_center_panel,ui_tr(UI_STR_CONTROL_CENTER),28,22,ui_font_h1(),HOME_TEXT);
 cc_label(control_center_panel,tr("常用设置，轻触即达","Everyday controls, one touch away"),28,66,ui_font_body_md(),HOME_TEXT_MUTED);
 lv_obj_t *settings=cc_surface(control_center_panel,w-162,23,134,42);lv_obj_add_flag(settings,LV_OBJ_FLAG_CLICKABLE);
 lv_obj_set_style_bg_opa(settings,LV_OPA_TRANSP,0);lv_obj_set_style_border_width(settings,0,0);
 cc_label(settings,LV_SYMBOL_SETTINGS,2,10,&lv_font_montserrat_24,HOME_TEXT);
 cc_label(settings,tr("全部设置","All settings"),38,10,ui_font_body_md(),HOME_TEXT);lv_obj_add_event_cb(settings,control_center_action_event,LV_EVENT_CLICKED,(void*)CONTROL_CENTER_ACTION_SETTINGS);
 cc_tile(24,97,tw,ui_tr(UI_STR_APP_WIFI),LV_SYMBOL_WIFI,HOME_CORAL,CONTROL_CENTER_ACTION_WIFI,&control_center_wifi_detail);
 cc_tile(36+tw,97,tw,tr("设备连接","Device connection"),LV_SYMBOL_REFRESH,HOME_BLUE,CONTROL_CENTER_ACTION_PAIRING,&center_connection);
 lv_obj_t *hdmi=cc_tile(24,208,tw,tr("HDMI 采集","HDMI capture"),LV_SYMBOL_VIDEO,HOME_BLUE,0,&center_hdmi_detail);
 control_center_hdmi_switch=lv_switch_create(hdmi);lv_obj_set_size(control_center_hdmi_switch,48,28);lv_obj_set_pos(control_center_hdmi_switch,tw-65,30);
 lv_obj_add_event_cb(control_center_hdmi_switch,on_control_center_hdmi_changed,LV_EVENT_VALUE_CHANGED,NULL);
 lv_obj_t *mute=cc_tile(36+tw,208,tw,tr("静音","Mute"),LV_SYMBOL_VOLUME_MID,HOME_BLUE,0,&mute_detail);
 mute_switch=lv_switch_create(mute);lv_obj_set_size(mute_switch,48,28);lv_obj_set_pos(mute_switch,tw-65,30);lv_obj_add_event_cb(mute_switch,cc_mute_event,LV_EVENT_VALUE_CHANGED,NULL);
 lv_obj_t *brightness_card=cc_surface(control_center_panel,24,319,tw,110);
 lv_obj_set_style_bg_color(brightness_card,lv_color_mix(HOME_SURFACE,HOME_CORAL,247),0);
 cc_sun(brightness_card);
 cc_label(brightness_card,tr("屏幕亮度","Brightness"),78,22,ui_font_h3(),HOME_TEXT);
 brightness_value=cc_label(brightness_card,"--",tw-68,22,ui_font_body_lg(),HOME_TEXT);
 brightness_slider=lv_slider_create(brightness_card);lv_obj_set_pos(brightness_slider,24,76);lv_obj_set_size(brightness_slider,tw-48,16);lv_slider_set_range(brightness_slider,0,100);
 lv_obj_set_style_bg_color(brightness_slider,HOME_CORAL,LV_PART_INDICATOR);lv_obj_set_style_bg_color(brightness_slider,lv_color_white(),LV_PART_KNOB);
 lv_obj_add_event_cb(brightness_slider,cc_slider_event,LV_EVENT_ALL,(void*)1);
 lv_obj_t *volume_card=cc_surface(control_center_panel,36+tw,319,tw,110);
 lv_obj_set_style_bg_color(volume_card,lv_color_mix(HOME_SURFACE,HOME_BLUE,247),0);
 cc_label(volume_card,LV_SYMBOL_VOLUME_MAX,22,23,&lv_font_montserrat_24,HOME_BLUE);
 cc_label(volume_card,tr("音量","Volume"),78,22,ui_font_h3(),HOME_TEXT);
 volume_value=cc_label(volume_card,"--",tw-68,22,ui_font_body_lg(),HOME_TEXT);
 volume_slider=lv_slider_create(volume_card);lv_obj_set_pos(volume_slider,24,76);lv_obj_set_size(volume_slider,tw-48,16);lv_slider_set_range(volume_slider,0,100);
 lv_obj_set_style_bg_color(volume_slider,HOME_BLUE,LV_PART_INDICATOR);lv_obj_set_style_bg_color(volume_slider,lv_color_white(),LV_PART_KNOB);lv_obj_add_event_cb(volume_slider,cc_slider_event,LV_EVENT_ALL,0);
 lv_obj_t *notice=cc_surface(control_center_panel,24,440,w-48,76);
 notice_row=notice;lv_obj_add_flag(notice,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(notice,notice_open,LV_EVENT_CLICKED,NULL);
 cc_label(notice,LV_SYMBOL_BELL,27,25,&lv_font_montserrat_24,HOME_GREEN);
 center_notice=cc_label(notice,tr("通知    暂无本地通知","Notifications    No local notifications"),96,25,ui_font_body_lg(),HOME_TEXT_MUTED);
 lv_obj_set_size(center_notice,w-172,48);lv_obj_set_y(center_notice,14);lv_label_set_long_mode(center_notice,LV_LABEL_LONG_DOT);
 center_error=cc_label(control_center_panel,"",28,81,ui_font_body_md(),HOME_CORAL);lv_obj_set_width(center_error,w-56);lv_label_set_long_mode(center_error,LV_LABEL_LONG_DOT);
 lv_obj_t *close=cc_surface(control_center_panel,w/2-60,572-37,120,32);lv_obj_add_flag(close,LV_OBJ_FLAG_CLICKABLE);lv_obj_set_style_border_width(close,0,0);lv_obj_set_style_bg_opa(close,LV_OPA_TRANSP,0);
 cc_label(close,tr("收起  ∧","Collapse  ^"),18,4,ui_font_body_md(),HOME_TEXT_MUTED);lv_obj_add_event_cb(close,cc_close,LV_EVENT_CLICKED,NULL);
 lv_obj_add_flag(control_center_overlay,LV_OBJ_FLAG_HIDDEN);
 static lv_timer_t *timer;if(!timer)timer=lv_timer_create(cc_status_tick,1000,NULL);
}

static void control_center_show(void)
{
    control_center_build();
    if (!control_center_overlay || !control_center_panel || control_center_visible) return;

    control_center_visible = true;
    if (control_center_chevron && lv_obj_is_valid(control_center_chevron))
        lv_label_set_text(control_center_chevron, LV_SYMBOL_UP);
    lv_obj_scroll_to_y(control_center_panel,0,LV_ANIM_OFF);
    cc_controls_read();
    cc_status();
    hdmi_switch_sync(hdmi_known?hdmi_actual:false);
    lv_obj_clear_flag(control_center_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(control_center_overlay);
    lv_anim_delete(control_center_panel, control_center_anim_y);
    lv_obj_set_y(control_center_panel, -CONTROL_CENTER_HEIGHT - 24);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, control_center_panel);
    lv_anim_set_values(&anim, -CONTROL_CENTER_HEIGHT - 24, HOME_SCREEN_W >= 1000 ? 87 : 24);
    lv_anim_set_time(&anim, 280);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&anim, control_center_anim_y);
    lv_anim_start(&anim);
}

static void control_center_trigger_event(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_CLICKED) {
        control_center_show();
    } else if (code == LV_EVENT_GESTURE) {
        lv_indev_t *indev = lv_indev_active();
        if (indev && lv_indev_get_gesture_dir(indev) == LV_DIR_BOTTOM) {
            control_center_show();
        }
    }
}

/* =======================
 * 状态栏
 * ======================= */
void ui_statusbar_create(lv_obj_t *parent)
{
    if(control_center_overlay && lv_obj_is_valid(control_center_overlay))lv_obj_delete(control_center_overlay);
    control_center_overlay=control_center_panel=control_center_hdmi_switch=NULL;
    control_center_visible=false;
    control_center_chevron=NULL;

    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, HOME_SCREEN_W, HOME_STATUS_H);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, HOME_SURFACE_RAISED, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(bar, HOME_BORDER, 0);
    lv_obj_set_style_border_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_color(bar, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(bar, LV_OPA_10, 0);
    lv_obj_set_style_shadow_width(bar, 0, 0);
    lv_obj_set_style_shadow_offset_y(bar, 3, 0);
    lv_obj_set_style_pad_hor(bar, 24, 0);
    lv_obj_set_style_pad_top(bar, 0, 0);
    lv_obj_set_style_pad_bottom(bar, 0, 0);
    lv_obj_set_style_pad_row(bar, 0, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(bar,
                           LV_FLEX_ALIGN_START,
                           LV_FLEX_ALIGN_START,
                           LV_FLEX_ALIGN_CENTER);
    apply_debug_outline(bar, lv_color_hex(0xE63946));

    lv_obj_t *top_row = lv_obj_create(bar);
    lv_obj_set_size(top_row, lv_pct(100), HOME_STATUS_H - 6);
    lv_obj_set_style_bg_opa(top_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_row, 0, 0);
    lv_obj_set_style_pad_all(top_row, 0, 0);
    lv_obj_set_style_pad_column(top_row, 0, 0);
    lv_obj_clear_flag(top_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(top_row, LV_LAYOUT_NONE);
    apply_debug_outline(top_row, lv_color_hex(0xFF7F11));

    lv_obj_t *left_status_group = lv_obj_create(top_row);
    lv_obj_set_size(left_status_group, 120, 62);
    lv_obj_align(left_status_group, LV_ALIGN_TOP_RIGHT, -130, 0);
    lv_obj_set_style_bg_opa(left_status_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left_status_group, 0, 0);
    lv_obj_set_style_pad_all(left_status_group, 0, 0);
    lv_obj_set_style_pad_column(left_status_group, 12, 0);
    lv_obj_clear_flag(left_status_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(left_status_group, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left_status_group,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    apply_debug_outline(left_status_group, lv_color_hex(0x8EC63F));

    lv_obj_t *brand = lv_label_create(top_row);
    lv_label_set_text(brand, "Mimi");
    lv_obj_set_pos(brand,0,10);
    lv_obj_set_width(brand, HOME_SCREEN_W >= 1000 ? 104 : 84);
    lv_obj_set_style_text_font(brand, ui_font_h1(), 0);
    lv_obj_set_style_text_color(brand, HOME_TEXT, 0);
    lv_obj_set_style_text_align(brand, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_clear_flag(brand, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *wifi_badge = lv_obj_create(left_status_group);
    lv_obj_set_size(wifi_badge, 48, 48);
    lv_obj_set_style_radius(wifi_badge, 0, 0);
    lv_obj_set_style_bg_opa(wifi_badge, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_badge, 0, 0);
    lv_obj_set_style_outline_width(wifi_badge, 0, 0);
    lv_obj_set_style_shadow_width(wifi_badge, 0, 0);
    lv_obj_set_style_pad_all(wifi_badge, 0, 0);
    lv_obj_clear_flag(wifi_badge, LV_OBJ_FLAG_SCROLLABLE);
    apply_debug_outline(wifi_badge, lv_color_hex(0x2A9D8F));

    wifi_icon_obj = lv_image_create(wifi_badge);
    lv_image_set_src(wifi_icon_obj, &wireless_network_wifi);
    lv_obj_set_size(wifi_icon_obj, 34, 34);
    lv_image_set_inner_align(wifi_icon_obj, LV_IMAGE_ALIGN_CONTAIN);
    lv_image_set_scale(wifi_icon_obj, LV_SCALE_NONE);
    lv_obj_set_style_image_recolor(wifi_icon_obj, HOME_TEXT, 0);
    lv_obj_set_style_image_recolor_opa(wifi_icon_obj, LV_OPA_COVER, 0);
    lv_obj_center(wifi_icon_obj);

    /* 100ask 云徽章：云图 + 右下角状态点 + 底边 OTA 下载进度条。
     * 点颜色：灰=离线 / 绿=已连 / 蓝=下载中 / 橙=待安装（见 ui_helpers）。 */
    lv_obj_t *cloud_badge = lv_obj_create(left_status_group);
    lv_obj_set_size(cloud_badge, 54, 48);
    lv_obj_set_style_radius(cloud_badge, 0, 0);
    lv_obj_set_style_bg_opa(cloud_badge, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cloud_badge, 0, 0);
    lv_obj_set_style_outline_width(cloud_badge, 0, 0);
    lv_obj_set_style_shadow_width(cloud_badge, 0, 0);
    lv_obj_set_style_pad_all(cloud_badge, 0, 0);
    lv_obj_set_layout(cloud_badge, LV_LAYOUT_NONE);
    lv_obj_clear_flag(cloud_badge, LV_OBJ_FLAG_SCROLLABLE);

    cloud_icon_obj = lv_image_create(cloud_badge);
    lv_image_set_src(cloud_icon_obj, &cloud);
    lv_obj_set_size(cloud_icon_obj, 40, 40);
    lv_image_set_inner_align(cloud_icon_obj, LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_set_style_image_recolor(cloud_icon_obj, HOME_TEXT, 0);
    lv_obj_set_style_image_recolor_opa(cloud_icon_obj, LV_OPA_COVER, 0);
    lv_obj_align(cloud_icon_obj, LV_ALIGN_TOP_MID, 0, 2);

    cloud_status_dot_obj = lv_obj_create(cloud_badge);
    lv_obj_set_size(cloud_status_dot_obj, 10, 10);
    lv_obj_set_style_radius(cloud_status_dot_obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(cloud_status_dot_obj, lv_color_hex(0xA6B2BA), 0);
    lv_obj_set_style_bg_opa(cloud_status_dot_obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cloud_status_dot_obj, 1, 0);
    lv_obj_set_style_border_color(cloud_status_dot_obj, HOME_SURFACE_RAISED, 0);
    lv_obj_set_style_border_opa(cloud_status_dot_obj, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(cloud_status_dot_obj, 0, 0);
    lv_obj_clear_flag(cloud_status_dot_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(cloud_status_dot_obj, LV_ALIGN_TOP_RIGHT, -4, 28);

    /* 2px track under the cloud; fill width driven by OTA progress (0–100). */
    cloud_progress_track_obj = lv_obj_create(cloud_badge);
    lv_obj_set_size(cloud_progress_track_obj, 40, 3);
    lv_obj_align(cloud_progress_track_obj, LV_ALIGN_BOTTOM_MID, 0, -1);
    lv_obj_set_style_radius(cloud_progress_track_obj, 2, 0);
    lv_obj_set_style_bg_color(cloud_progress_track_obj, HOME_BORDER, 0);
    lv_obj_set_style_bg_opa(cloud_progress_track_obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cloud_progress_track_obj, 0, 0);
    lv_obj_set_style_pad_all(cloud_progress_track_obj, 0, 0);
    lv_obj_set_layout(cloud_progress_track_obj, LV_LAYOUT_NONE);
    lv_obj_clear_flag(cloud_progress_track_obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(cloud_progress_track_obj, LV_OBJ_FLAG_HIDDEN);

    cloud_progress_fill_obj = lv_obj_create(cloud_progress_track_obj);
    lv_obj_set_size(cloud_progress_fill_obj, 0, 3);
    lv_obj_align(cloud_progress_fill_obj, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(cloud_progress_fill_obj, 2, 0);
    lv_obj_set_style_bg_color(cloud_progress_fill_obj, lv_color_hex(0x5B9BD5), 0);
    lv_obj_set_style_bg_opa(cloud_progress_fill_obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cloud_progress_fill_obj, 0, 0);
    lv_obj_set_style_pad_all(cloud_progress_fill_obj, 0, 0);
    lv_obj_clear_flag(cloud_progress_fill_obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* 顶栏中间：INA219 整机功耗/电流/电压。相对 top_row 绝对居中，不受左右区域宽度影响。
     * 主题主色纯色（随主题切换），不上云，仅本机显示。 */
    lv_obj_t *mid_col = lv_obj_create(top_row);
    lv_obj_set_size(mid_col, TOP_POWER_LABEL_WIDTH, lv_pct(100));
    lv_obj_align(mid_col, LV_ALIGN_CENTER, 0, 0);
    if (HOME_SCREEN_W < 1000) {
        lv_obj_set_size(mid_col, lv_pct(100), 44);
        lv_obj_align(mid_col, LV_ALIGN_BOTTOM_MID, 0, 0);
    }
    lv_obj_set_style_bg_opa(mid_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mid_col, 0, 0);
    lv_obj_set_style_pad_all(mid_col, 0, 0);
    lv_obj_clear_flag(mid_col, LV_OBJ_FLAG_SCROLLABLE);
    /* Power readout is display-only; don't steal taps from the HDMI switch. */
    lv_obj_clear_flag(mid_col, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(mid_col, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(mid_col,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    apply_debug_outline(mid_col, lv_color_hex(0x06D6A0));

    power_label = lv_label_create(mid_col);
    lv_label_set_text(power_label, "--.-V ---mA --.-W");
    lv_label_set_long_mode(power_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(power_label, TOP_POWER_LABEL_WIDTH);
    lv_obj_set_style_text_font(power_label, ui_font_h3(), 0);
    lv_obj_set_style_text_color(power_label, lv_color_hex(0xE0E3EC), 0);
    lv_obj_set_style_text_align(power_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_opa(power_label, LV_OPA_90, 0);
    lv_obj_clear_flag(power_label, LV_OBJ_FLAG_CLICKABLE);
    /* The redesigned home overlays time/date in the center. Keep the power
     * object alive for backend updates and expose its detail in Control
     * Center, but do not paint two competing readouts in the status bar. */
    lv_obj_add_flag(power_label, LV_OBJ_FLAG_HIDDEN);

    /* Tap the center readout or swipe down to reveal the control center. */
    lv_obj_add_flag(mid_col, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(mid_col, 8);
    lv_obj_add_event_cb(mid_col, control_center_trigger_event, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(mid_col, control_center_trigger_event, LV_EVENT_GESTURE, NULL);

    lv_obj_t *pull_handle = lv_obj_create(mid_col);
    lv_obj_set_size(pull_handle, 40, 4);
    lv_obj_align(pull_handle, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_style_radius(pull_handle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(pull_handle, HOME_TEXT_MUTED, 0);
    lv_obj_set_style_bg_opa(pull_handle, LV_OPA_30, 0);
    lv_obj_add_flag(pull_handle,LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_border_width(pull_handle, 0, 0);
    lv_obj_set_style_pad_all(pull_handle, 0, 0);
    lv_obj_clear_flag(pull_handle, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *notifications=lv_obj_create(top_row);
    lv_obj_remove_style_all(notifications);lv_obj_set_size(notifications,48,48);
    lv_obj_align(notifications,LV_ALIGN_TOP_RIGHT,-60,8);lv_obj_add_flag(notifications,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *bell=cc_label(notifications,LV_SYMBOL_BELL,0,0,&lv_font_montserrat_24,HOME_TEXT);lv_obj_center(bell);
    lv_obj_add_event_cb(notifications,control_center_trigger_event,LV_EVENT_CLICKED,NULL);
    lv_obj_t *expand=lv_obj_create(top_row);lv_obj_remove_style_all(expand);lv_obj_set_size(expand,48,48);
    lv_obj_align(expand,LV_ALIGN_TOP_RIGHT,4,8);lv_obj_add_flag(expand,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *arrow=cc_label(expand,LV_SYMBOL_DOWN,0,0,&lv_font_montserrat_16,HOME_TEXT_MUTED);lv_obj_center(arrow);control_center_chevron=arrow;
    lv_obj_add_event_cb(expand,control_center_trigger_event,LV_EVENT_CLICKED,NULL);
    lv_obj_add_event_cb(bar,control_center_trigger_event,LV_EVENT_GESTURE,NULL);
    /* Retain an invisible compatibility handle; the only visible home switch is in Control Center. */
    hdmi_switch_obj=lv_switch_create(bar);lv_obj_add_flag(hdmi_switch_obj,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(hdmi_switch_obj,on_hdmi_switch_changed,LV_EVENT_VALUE_CHANGED,NULL);

    /* 初始开关态取自持久化设置（默认关闭）。 */
    hdmi_switch_sync(sys_settings_get()->hdmi_enabled);

    refresh_wifi_icon_visual();
}

/* =======================
 * HDMI 显示模式开关
 * ======================= */

static void hdmi_clear_pending(void)
{
    hdmi_toggle_pending = false;
    if (hdmi_pending_timer) {
        lv_timer_del(hdmi_pending_timer);
        hdmi_pending_timer = NULL;
    }
}

static void hdmi_pending_timeout_cb(lv_timer_t *timer)
{
    (void)timer;
    hdmi_pending_timer = NULL;
    if (!hdmi_toggle_pending) return;
    hdmi_toggle_pending = false;
    hdmi_switch_sync(hdmi_known?hdmi_actual:false);
    if(center_error && lv_obj_is_valid(center_error))lv_label_set_text(center_error,tr("HDMI 未响应，请检查连接","HDMI did not respond; check connection"));
    cc_status();
}

static void hdmi_switch_apply_visual(lv_obj_t *sw, bool enabled)
{
    if (!sw || !lv_obj_is_valid(sw)) return;
    if (!desktop_hdmi_supported()) {
        enabled = false;
        lv_obj_add_state(sw, LV_STATE_DISABLED);
    }
    bool checked = lv_obj_has_state(sw, LV_STATE_CHECKED);
    if (checked == enabled) return;

    hdmi_ignore_event = true;
    if (enabled)
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    else
        lv_obj_clear_state(sw, LV_STATE_CHECKED);
    hdmi_ignore_event = false;
}

static void hdmi_switch_sync(bool enabled)
{
    hdmi_switch_apply_visual(hdmi_switch_obj, enabled);
    hdmi_switch_apply_visual(hdmi_overlay_switch, enabled);
    hdmi_switch_apply_visual(control_center_hdmi_switch, enabled);
}

static void hdmi_send_enable(bool on)
{
    if (!desktop_hdmi_supported()) {
        hdmi_switch_sync(false);
        return;
    }
    hdmi_toggle_pending = true;
    hdmi_toggle_target = on;

    hdmi_switch_sync(hdmi_known?hdmi_actual:false);
    cc_status();

    if (hdmi_pending_timer) {
        lv_timer_del(hdmi_pending_timer);
        hdmi_pending_timer = NULL;
    }
    hdmi_pending_timer = lv_timer_create(hdmi_pending_timeout_cb, 2500, NULL);
    lv_timer_set_repeat_count(hdmi_pending_timer, 1);

    hdmi_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.action = on ? HDMI_CMD_ENABLE : HDMI_CMD_DISABLE;
    mw_publish(TOPIC_HDMI_COMMAND, &cmd, sizeof(cmd), MW_DIR_UI_TO_BACKEND);
    printf("[HDMI] request enable=%d\n", on ? 1 : 0);
}

static void on_hdmi_switch_changed(lv_event_t *e)
{
    if (hdmi_ignore_event) return;

    lv_obj_t *sw = lv_event_get_target(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);

    /* Always accept the latest user intent; do not lock the switch. */
    hdmi_send_enable(on);
}

static void on_hdmi_overlay_switch_changed(lv_event_t *e)
{
    if (hdmi_ignore_event) return;
    lv_obj_t *sw = lv_event_get_target(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    hdmi_send_enable(on);
}

static void on_control_center_hdmi_changed(lv_event_t *e)
{
    if (hdmi_ignore_event) return;
    lv_obj_t *sw = lv_event_get_target(e);
    hdmi_send_enable(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

/* Floating control while the transparent HDMI screen covers the desktop. */
static void hdmi_overlay_build(lv_obj_t *screen)
{
    if (!screen) return;

    lv_obj_t *bar = lv_obj_create(screen);
    lv_obj_set_size(bar, 220, 52);
    lv_obj_align(bar, LV_ALIGN_TOP_RIGHT, -16, 16);
    lv_obj_set_style_radius(bar, 16, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x303548), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_80, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(0x464C61), 0);
    lv_obj_set_style_pad_hor(bar, 14, 0);
    lv_obj_set_style_pad_ver(bar, 0, 0);
    lv_obj_set_style_pad_column(bar, 10, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *label = lv_label_create(bar);
    lv_label_set_text(label, ui_tr(UI_STR_DISPLAY_MODE));
    lv_obj_set_style_text_font(label, ui_font_body_md(), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xF7F8FC), 0);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);

    hdmi_overlay_switch = lv_switch_create(bar);
    lv_obj_set_size(hdmi_overlay_switch, 52, 28);
    lv_obj_set_ext_click_area(hdmi_overlay_switch, 12);
    lv_obj_set_style_bg_color(hdmi_overlay_switch, UI_BORDER_DEFAULT, LV_PART_MAIN);
    lv_obj_set_style_bg_color(hdmi_overlay_switch, UI_COLOR_PRIMARY,
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(hdmi_overlay_switch, LV_OPA_COVER,
                            LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(hdmi_overlay_switch, lv_color_white(), LV_PART_KNOB);
    hdmi_switch_apply_visual(hdmi_overlay_switch, true);
    lv_obj_add_event_cb(hdmi_overlay_switch, on_hdmi_overlay_switch_changed,
                        LV_EVENT_VALUE_CHANGED, NULL);
}

/* 由 hdmi_preview_status_callback 调用：同步开关、ack 后持久化、显示/隐藏 HDMI 透明屏。 */
void hdmi_ui_handle_status(const hdmi_preview_status_t *status)
{
    if (!status) return;
    if (!desktop_hdmi_supported()) {
        hdmi_switch_sync(false);
        return;
    }

    hdmi_actual=status->enabled;hdmi_known=true;
    if (hdmi_toggle_pending) {
        if (status->enabled == hdmi_toggle_target) {
            sys_settings_get()->hdmi_enabled=status->enabled;
            if(sys_settings_save_checked()!=0 && center_error && lv_obj_is_valid(center_error))
                lv_label_set_text(center_error,tr("HDMI 已切换，保存失败","HDMI changed; saving failed"));
            hdmi_clear_pending();
            hdmi_switch_sync(status->enabled);
        }
        /* While waiting, ignore opposite enabled frames so the knob does not bounce. */
    } else {
        hdmi_switch_sync(status->enabled);
    }

    cc_status();
    if (status->active) {
        app_manager_show_hdmi_preview_screen();
        /* Attach floating switch once the transparent screen is active. */
        lv_obj_t *scr = lv_scr_act();
        if (scr && !hdmi_overlay_switch) {
            hdmi_overlay_build(scr);
        } else {
            hdmi_switch_apply_visual(hdmi_overlay_switch, status->enabled);
        }
    } else {
        hdmi_overlay_switch = NULL;
        app_manager_hide_hdmi_preview_screen();
    }
}
