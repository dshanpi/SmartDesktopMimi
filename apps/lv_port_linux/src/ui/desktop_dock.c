#include "desktop_dock.h"
#include "desktop_home_tokens.h"
#include "desktop_update.h"
#include "ui_fonts.h"
#include "ui_i18n.h"
#include "../system/app_manager.h"
#include "../system/app_registry.h"

/* The home application strip stays below the two main cards. Swiping changes
 * the visible applications in place; there is no second navigation bar. */
static const app_registry_entry_t update_entry = {
    .id=APP_ID_NONE, .name="Desktop Update", .name_id=UI_STR_HOME_UPDATES,
    .accent_color=0x30B27A, .show_in_launcher=true,
};

static void open_app(lv_event_t *e)
{
    const app_registry_entry_t *entry=lv_event_get_user_data(e);
    if (entry == &update_entry) desktop_update_show();
    else if (entry) app_manager_open(entry->id);
}

static const char *symbol_for(app_id_t id)
{
    switch (id) {
    case APP_ID_WIFI: return LV_SYMBOL_WIFI;
    case APP_ID_SETTING: return LV_SYMBOL_SETTINGS;
    case APP_ID_NONE: case APP_ID_OTA: return LV_SYMBOL_DOWNLOAD;
    case APP_ID_IOT: return LV_SYMBOL_HOME;
    case APP_ID_PAIRING: return LV_SYMBOL_REFRESH;
    case APP_ID_AI: case APP_ID_VOICE: return LV_SYMBOL_AUDIO;
    case APP_ID_VIDEO: case APP_ID_HDMI_MCP: return LV_SYMBOL_VIDEO;
    case APP_ID_BT: return LV_SYMBOL_BLUETOOTH;
    default: return LV_SYMBOL_LIST;
    }
}

static void page_changed(lv_event_t *e)
{
    lv_obj_t *strip=lv_event_get_target(e);
    lv_obj_t *pager=lv_event_get_user_data(e);
    int32_t width=lv_obj_get_content_width(strip);
    int32_t count=(int32_t)lv_obj_get_child_count(pager);
    if (width <= 0 || count == 0) return;
    int32_t page=(lv_obj_get_scroll_x(strip)+width/2)/width;
    if (page < 0) page=0;
    if (page >= count) page=count-1;
    for (int32_t i=0;i<count;i++) {
        lv_obj_t *line=lv_obj_get_child(pager,i);
        lv_obj_set_style_bg_color(line,i==page?HOME_BLUE:HOME_TEXT_MUTED,0);
        lv_obj_set_style_bg_opa(line,i==page?LV_OPA_COVER:LV_OPA_30,0);
    }
}

/* Native LVGL shapes keep icons crisp without a platform image decoder.
 * Decorative children never take touch focus from the application button. */
static lv_obj_t *icon_shape(lv_obj_t *parent,int x,int y,int w,int h,int radius,lv_color_t color)
{
    lv_obj_t *shape=lv_obj_create(parent);
    lv_obj_remove_style_all(shape);
    lv_obj_set_pos(shape,x,y);
    lv_obj_set_size(shape,w,h);
    lv_obj_set_style_radius(shape,radius,0);
    lv_obj_set_style_bg_color(shape,color,0);
    lv_obj_set_style_bg_opa(shape,LV_OPA_COVER,0);
    lv_obj_clear_flag(shape,LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return shape;
}

static void create_icon(lv_obj_t *card,const app_registry_entry_t *entry)
{
    uint32_t tint=0xECE7F7,ink=0x75619D;
    switch(entry->id) {
    case APP_ID_HDMI_MCP: tint=0xE1E5F6;ink=0x535FA9;break;
    case APP_ID_AI: case APP_ID_WIFI: tint=0xFFE0DE;ink=0xF36460;break;
    case APP_ID_IOT: tint=0xFFE2CC;ink=0xFF8845;break;
    case APP_ID_NONE: case APP_ID_OTA: tint=0xD9EEE9;ink=0x38998B;break;
    default: break;
    }
    lv_color_t fg=lv_color_hex(ink),bg=lv_color_hex(tint);
    lv_obj_t *tile=icon_shape(card,0,0,58,58,17,bg);
    lv_obj_align(tile,LV_ALIGN_TOP_MID,0,0);
    if (entry->id==APP_ID_HDMI_MCP) {
        lv_obj_t *screen=icon_shape(tile,12,16,34,24,3,bg);
        lv_obj_set_style_border_width(screen,3,0);
        lv_obj_set_style_border_color(screen,fg,0);
        icon_shape(tile,27,40,4,4,0,fg);
        icon_shape(tile,21,43,16,3,1,fg);
    } else if (entry->id==APP_ID_AI) {
        icon_shape(tile,12,13,35,32,16,fg);
        icon_shape(tile,33,33,17,16,8,bg);
        icon_shape(tile,19,22,5,5,3,bg);
        icon_shape(tile,27,17,5,5,3,bg);
        icon_shape(tile,35,22,5,5,3,bg);
        icon_shape(tile,17,30,5,5,3,bg);
    } else {
        lv_obj_t *glyph=lv_label_create(tile);
        lv_label_set_text(glyph,symbol_for(entry->id));
        lv_obj_set_style_text_font(glyph,&lv_font_montserrat_28,0);
        lv_obj_set_style_text_color(glyph,fg,0);
        lv_obj_center(glyph);
    }
}

static void card_key(lv_event_t *e)
{
    uint32_t key=lv_event_get_key(e);
    lv_obj_t *card=lv_event_get_target(e);
    lv_obj_t *page=lv_obj_get_parent(card), *strip=lv_obj_get_parent(page);
    int32_t index=lv_obj_get_index(card), page_index=lv_obj_get_index(page);
    /* Enter uses the button input handler and its single CLICKED event. */
    if (key!=LV_KEY_LEFT && key!=LV_KEY_RIGHT) return;
    index += key==LV_KEY_RIGHT ? 1 : -1;
    if (index < 0 && page_index > 0) {
        page=lv_obj_get_child(strip,page_index-1);
        index=(int32_t)lv_obj_get_child_count(page)-1;
    } else if (index >= (int32_t)lv_obj_get_child_count(page) &&
               page_index+1 < (int32_t)lv_obj_get_child_count(strip)) {
        page=lv_obj_get_child(strip,page_index+1); index=0;
    }
    if (index < 0 || index >= (int32_t)lv_obj_get_child_count(page)) return;
    card=lv_obj_get_child(page,index);
    lv_group_focus_obj(card);
    lv_obj_scroll_to_view_recursive(card,LV_ANIM_ON);
}

static void card_focused(lv_event_t *e)
{
    lv_indev_t *input=lv_indev_active();
    /* Pointer focus must not start a second scroll animation during a drag. */
    if (input && (lv_indev_get_type(input)==LV_INDEV_TYPE_KEYPAD ||
                  lv_indev_get_type(input)==LV_INDEV_TYPE_ENCODER))
        lv_obj_scroll_to_view_recursive(lv_event_get_target(e),LV_ANIM_ON);
}

static void create_card(lv_obj_t *page,const app_registry_entry_t *entry,int32_t width)
{
    lv_obj_t *card=lv_button_create(page);
    lv_obj_set_size(card,width,106);
    lv_obj_set_style_bg_opa(card,LV_OPA_TRANSP,0);
    lv_obj_set_style_shadow_width(card,0,0);
    lv_obj_set_style_border_width(card,0,0);
    lv_obj_set_style_pad_all(card,8,0);
    lv_obj_set_style_bg_color(card,HOME_SURFACE_PRESSED,LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(card,LV_OPA_COVER,LV_STATE_PRESSED);
    lv_obj_set_style_outline_color(card,HOME_BLUE,LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(card,2,LV_STATE_FOCUSED);
    lv_obj_clear_flag(card,LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_flag(card,LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(card,open_app,LV_EVENT_CLICKED,(void *)entry);
    lv_obj_add_event_cb(card,card_key,LV_EVENT_KEY,(void *)entry);
    lv_obj_add_event_cb(card,card_focused,LV_EVENT_FOCUSED,NULL);
    create_icon(card,entry);
    lv_obj_t *name=lv_label_create(card);
    lv_label_set_text(name,ui_tr(entry->name_id));
    lv_obj_set_width(name,width-16);
    lv_label_set_long_mode(name,LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(name,ui_font_body_md(),0);
    lv_obj_set_style_text_color(name,HOME_TEXT,0);
    lv_obj_set_style_text_align(name,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_align(name,LV_ALIGN_TOP_MID,0,66);
    lv_group_t *group=lv_group_get_default();
    if (group) lv_group_add_obj(group,card);
}

void ui_dock_create(lv_obj_t *parent)
{
    const app_registry_entry_t *entries[APP_ID_MAX+1];
    size_t count=0;
    const app_id_t first[]={APP_ID_HDMI_MCP,APP_ID_AI,APP_ID_IOT,APP_ID_WIFI};
    for (size_t i=0;i<sizeof(first)/sizeof(first[0]);i++) {
        const app_registry_entry_t *entry=first[i]==APP_ID_NONE?&update_entry:app_registry_get_entry(first[i]);
        if (entry && entry->show_in_launcher) entries[count++]=entry;
    }
    for (size_t i=0;i<app_registry_get_launcher_count();i++) {
        const app_registry_entry_t *entry=app_registry_get_launcher_entry(i);
        if (entry->id!=APP_ID_HDMI_MCP && entry->id!=APP_ID_AI && entry->id!=APP_ID_IOT && entry->id!=APP_ID_WIFI)
            entries[count++]=entry;
    }
    entries[count++]=&update_entry;
    lv_obj_t *panel=lv_obj_create(parent);
    lv_obj_set_pos(panel,HOME_CONTENT_X,HOME_DOCK_Y);
    lv_obj_set_size(panel,HOME_CONTENT_W,HOME_DOCK_H);
    lv_obj_set_style_radius(panel,HOME_RADIUS,0);
    lv_obj_set_style_bg_color(panel,HOME_SURFACE,0);
    lv_obj_set_style_border_color(panel,HOME_BORDER,0);
    lv_obj_set_style_border_width(panel,1,0);
    lv_obj_set_style_pad_all(panel,12,0);
    lv_obj_clear_flag(panel,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *strip=lv_obj_create(panel);
    lv_obj_set_size(strip,lv_pct(100),112);
    lv_obj_set_style_pad_all(strip,0,0);
    lv_obj_set_style_pad_column(strip,0,0);
    lv_obj_set_style_border_width(strip,0,0);
    lv_obj_set_style_bg_opa(strip,LV_OPA_TRANSP,0);
    lv_obj_set_flex_flow(strip,LV_FLEX_FLOW_ROW);
    lv_obj_set_scroll_dir(strip,LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(strip,LV_SCROLL_SNAP_START);
    lv_obj_set_scrollbar_mode(strip,LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(strip,LV_OBJ_FLAG_SCROLL_ONE);
    lv_obj_clear_flag(strip,LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_update_layout(panel);
    int32_t page_width=lv_obj_get_content_width(strip);
    int32_t columns=HOME_SCREEN_W>=1000?4:2;
    int32_t page_count=(int32_t)((count+columns-1)/columns);
    lv_obj_t *pager=lv_obj_create(panel);
    lv_obj_remove_style_all(pager);
    lv_obj_set_size(pager,page_count*28+(page_count-1)*8,4);
    lv_obj_clear_flag(pager,LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(pager,LV_ALIGN_BOTTOM_MID,0,-5);
    for (int32_t i=0;i<page_count;i++)
        icon_shape(pager,i*36,0,28,4,2,HOME_TEXT_MUTED);
    lv_obj_t *page=NULL;
    for (size_t i=0;i<count;i++) {
        if (i%(size_t)columns==0) {
            page=lv_obj_create(strip);
            lv_obj_set_size(page,page_width,112);
            lv_obj_set_style_pad_all(page,2,0);
            lv_obj_set_style_pad_column(page,4,0);
            lv_obj_set_style_border_width(page,0,0);
            lv_obj_set_style_bg_opa(page,LV_OPA_TRANSP,0);
            lv_obj_set_flex_flow(page,LV_FLEX_FLOW_ROW);
            lv_obj_add_flag(page,LV_OBJ_FLAG_SNAPPABLE);
            lv_obj_clear_flag(page,LV_OBJ_FLAG_SCROLLABLE);
        }
        create_card(page,entries[i],(page_width-4-4*(columns-1))/columns);
    }
    lv_obj_add_event_cb(strip,page_changed,LV_EVENT_SCROLL_END,pager);
    lv_obj_send_event(strip,LV_EVENT_SCROLL_END,NULL);
}
