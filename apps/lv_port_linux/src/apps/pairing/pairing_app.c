#include "pairing_app.h"
#include "../../system/app_manager.h"
#include "../../system/backend_types.h"
#include "../../system/cloud_pairing_status.h"
#include "../../middleware/middleware.h"
#include "../../ui/mimi_ui.h"
#include "../../ui/ui_fonts.h"
#include <string.h>
#include <errno.h>

/* The desktop only handles the existing public status contract. Device
 * signatures, provisioning and long-lived secrets remain in the Provider. */
static lv_obj_t *code_label, *state_label, *expiry_label, *refresh_button, *steps_label, *refresh_label;
static lv_timer_t *pair_timer;
static cloud_status_t snapshot;
static cloud_pairing_countdown_t countdown;
static cloud_pairing_status_v1_t ownership;
static bool ownership_received;
static bool active, received;

static const char *tr(const char *zh, const char *en) { return ui_i18n_is_zh() ? zh : en; }
static void request_status(void)
{
    cloud_cmd_t cmd = {.action = CLOUD_CMD_GET_STATUS};
    mw_publish(TOPIC_CLOUD_COMMAND, &cmd, sizeof(cmd), MW_DIR_UI_TO_BACKEND);
}
static bool valid_code(const char *code)
{
    for (int i=0; i<6; ++i) if (code[i] < '0' || code[i] > '9') return false;
    return code[6] == '\0';
}
static void render_status(void)
{
    if (!active || !code_label) return;
    uint32_t remaining = cloud_pairing_remaining(&countdown,lv_tick_get());
    bool unavailable = snapshot.state == CLOUD_STATE_UNAVAILABLE;
    bool bound = ownership_received && ownership.ownership==CLOUD_PAIRING_BOUND;
    lv_obj_t *binding_controls[]={code_label,expiry_label,steps_label};
    for (unsigned i=0;i<sizeof(binding_controls)/sizeof(binding_controls[0]);i++) {
        if (!binding_controls[i]) continue;
        if (bound && !unavailable) lv_obj_add_flag(binding_controls[i],LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(binding_controls[i],LV_OBJ_FLAG_HIDDEN);
    }
    bool usable = received && !unavailable && !bound && remaining && valid_code(countdown.code) && !snapshot.bind_token_refreshing;
    lv_label_set_text(code_label, usable ? countdown.code : tr("— — — — — —", "- - - - - -"));
    if (usable) lv_label_set_text_fmt(expiry_label, ui_i18n_is_zh() ? "有效期 %02u:%02u" : "Expires in %02u:%02u", remaining/60, remaining%60);
    else lv_label_set_text(expiry_label, received && valid_code(countdown.code) && !remaining ? tr("绑定码已过期，请刷新", "Code expired. Refresh to continue.") : "");
    const char *state;
    if (!received) state=tr("正在读取设备状态…", "Reading device status…");
    else if (unavailable) state=tr("当前系统未提供配对服务", "Pairing service is unavailable on this system.");
    else if (snapshot.bind_token_refreshing) state=tr("正在读取配对信息…", "Reading pairing information…");
    else if (bound) state=tr("设备已绑定账号，无需绑定码。账号归属和解绑请在 100ask.net 查看。", "Device already linked. Check ownership or unlink at 100ask.net.");
    else if (snapshot.bind_token_error == 401 || snapshot.bind_token_error == 403)
        state=tr("设备身份认证失败，请检查出厂登记", "Device authentication failed. Check factory enrollment.");
    else if (snapshot.bind_token_error == -ENOKEY)
        state=tr("缺少出厂设备身份，请完成受控登记", "Factory identity missing. Complete controlled enrollment.");
    else if (snapshot.bind_token_error == 200)
        state=tr("暂未取得可用绑定码。若设备已绑定，可继续使用；账号归属请在 100ask.net 查看。", "No usable pairing code was returned. If already linked, continue using the device. Check account ownership at 100ask.net.");
    else if (snapshot.bind_token_error)
        state=tr("暂时无法获取绑定码，请检查网络后重试", "Cannot get a code. Check the network and retry.");
    else if (ownership_received && ownership.ownership==CLOUD_PAIRING_UNKNOWN)
        state=tr("账号归属尚未确认。可刷新查询，或在 100ask.net 查看设备绑定。", "Account ownership is unconfirmed. Refresh to check, or review device binding at 100ask.net.");
    else if (usable) state=tr("在 100ask.net 输入下方绑定码", "Enter this code at 100ask.net.");
    else state=tr("点击刷新绑定码开始配对", "Select Refresh code to pair.");
    lv_label_set_text(state_label,state);
    lv_label_set_text(refresh_label,bound ? tr("检查绑定状态","Check binding status") : tr("刷新绑定码","Refresh code"));
    if (!received || unavailable || snapshot.bind_token_refreshing) lv_obj_add_state(refresh_button,LV_STATE_DISABLED);
    else lv_obj_remove_state(refresh_button,LV_STATE_DISABLED);
}
static void status_cb(const mw_msg_t *msg)
{
    if (!active || !msg || msg->topic!=TOPIC_CLOUD_STATUS || !msg->data || msg->data_len!=sizeof(cloud_status_t)) return;
    memcpy(&snapshot,msg->data,sizeof(snapshot));
    snapshot.bind_token[sizeof(snapshot.bind_token)-1]='\0';
    received=true;
    if (!ownership_received) cloud_pairing_observe(&countdown,snapshot.bind_token,snapshot.bind_token_expires_in,lv_tick_get());
    render_status();
}
static void ownership_cb(const mw_msg_t *msg)
{
    if (!active || !msg || msg->topic!=TOPIC_CLOUD_PAIRING_STATUS || msg->data_len!=sizeof(cloud_pairing_status_v1_t)) return;
    cloud_pairing_status_v1_t value;
    memcpy(&value,msg->data,sizeof(value));
    if (!cloud_pairing_status_valid(&value)) return;
    ownership=value;ownership_received=true;
    cloud_pairing_observe(&countdown,value.code,value.expires_in,lv_tick_get());
    render_status();
}
static void timer_cb(lv_timer_t *timer)
{
    (void)timer;render_status();
    /* Also recover from a dropped IPC reply without issuing fresh codes. */
    static unsigned polls;
    if (++polls % 10 == 0) request_status();
}
static void refresh_cb(lv_event_t *e)
{
    (void)e;
    if (!received || snapshot.bind_token_refreshing || snapshot.state==CLOUD_STATE_UNAVAILABLE) return;
    memset(&countdown,0,sizeof(countdown));
    memset(snapshot.bind_token,0,sizeof(snapshot.bind_token));snapshot.bind_token_expires_in=0;
    snapshot.bind_token_refreshing=true;render_status();
    cloud_cmd_t cmd={.action=CLOUD_CMD_REFRESH_BIND_TOKEN};
    mw_publish(TOPIC_CLOUD_COMMAND,&cmd,sizeof(cmd),MW_DIR_UI_TO_BACKEND);
}
static void back_cb(lv_event_t *e) { (void)e;app_manager_back_home(); }
static void close_pairing(void)
{
    active=false;mw_unsubscribe(TOPIC_CLOUD_STATUS,status_cb);
    mw_unsubscribe(TOPIC_CLOUD_PAIRING_STATUS,ownership_cb);
    if(pair_timer){lv_timer_delete(pair_timer);pair_timer=NULL;}
    memset(&snapshot,0,sizeof(snapshot));
    memset(&countdown,0,sizeof(countdown));memset(&ownership,0,sizeof(ownership));ownership_received=false;
    code_label=state_label=expiry_label=refresh_button=steps_label=refresh_label=NULL;
}
static void init_pairing(void)
{
    close_pairing();received=false;active=true;
    lv_obj_t *screen=lv_obj_create(NULL);
    mimi_ui_header(screen,ui_tr(UI_STR_APP_PAIRING),back_cb);
    lv_obj_t *body=mimi_ui_body(screen);
    lv_obj_add_flag(body,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(body,LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body,18,0);
    lv_obj_t *card=lv_obj_create(body);mimi_ui_surface(card,18);
    lv_obj_set_width(card,lv_pct(100));lv_obj_set_height(card,LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(card,24,0);lv_obj_set_style_pad_row(card,16,0);
    lv_obj_set_flex_flow(card,LV_FLEX_FLOW_COLUMN);
    state_label=mimi_ui_text(card,"",ui_font_body_lg(),MIMI_TEXT);
    lv_obj_set_width(state_label,lv_pct(100));lv_label_set_long_mode(state_label,LV_LABEL_LONG_WRAP);
    code_label=mimi_ui_text(card,"",ui_font_h1(),MIMI_TEXT);
    expiry_label=mimi_ui_text(card,"",ui_font_body_md(),MIMI_MUTED);
    refresh_button=lv_button_create(card);mimi_ui_button_style(refresh_button,true);
    lv_obj_set_size(refresh_button,240,56);
    lv_obj_t *label=refresh_label=mimi_ui_text(refresh_button,tr("刷新绑定码","Refresh code"),ui_font_body_lg(),MIMI_TEXT);lv_obj_center(label);
    lv_obj_add_event_cb(refresh_button,refresh_cb,LV_EVENT_CLICKED,NULL);
    lv_obj_t *steps=steps_label=mimi_ui_text(body,tr("1. 在手机或电脑登录 100ask.net\n2. 在设备管理中输入六位绑定码\n3. 在网站确认设备归属\n\n账号绑定结果与解绑请在网站查看。云连接正常不代表已绑定账号。", "1. Sign in to 100ask.net on your phone or computer.\n2. Enter the six-digit code in device management.\n3. Confirm device ownership on the website.\n\nCheck account binding or unlink on the website. A cloud connection does not establish account ownership."),ui_font_body_lg(),MIMI_MUTED);
    lv_obj_set_width(steps,lv_pct(100));lv_label_set_long_mode(steps,LV_LABEL_LONG_WRAP);
    mw_subscribe(TOPIC_CLOUD_STATUS,status_cb);
    mw_subscribe(TOPIC_CLOUD_PAIRING_STATUS,ownership_cb);
    pair_timer=lv_timer_create(timer_cb,1000,NULL);
    lv_scr_load(screen);render_status();request_status();
}
AppDescriptor app_pairing={.id=APP_ID_PAIRING,.name="100ask.net Pairing",.init=init_pairing,.close=close_pairing};
