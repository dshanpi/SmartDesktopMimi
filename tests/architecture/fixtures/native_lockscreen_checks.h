/* Included by the native UI harness; exercises real LVGL input dispatch. */
static lv_indev_state_t touch_state, key_state;
static int underlying_keys;
static int touch_x=170, touch_y=455;
static void touch_read(lv_indev_t *input, lv_indev_data_t *data) {
 (void)input;data->point.x=touch_x;data->point.y=touch_y;data->state=touch_state;
}
static void key_read(lv_indev_t *input, lv_indev_data_t *data) {
 (void)input;data->key=LV_KEY_ENTER;data->state=key_state;
}
static void key_wake(lv_event_t *event) {
 lv_indev_t *input=lv_event_get_target(event);
 if(lv_indev_get_state(input)==LV_INDEV_STATE_PRESSED)ui_lockscreen_handle_key(input);
}
static void count_key(lv_event_t *event) {(void)event;underlying_keys++;}
static void test_lockscreen(void) {
 assert(ui_lockscreen_is_active());
 assert(find_label(lv_screen_active(),ui_tr(UI_STR_LOCKSCREEN_WAKE)));
 ui_lockscreen_wake();lv_tick_inc(500);pump();
 lv_obj_t *home=lv_screen_active();
 lv_indev_t *pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);
 lv_indev_set_read_cb(pointer,touch_read);touch_state=LV_INDEV_STATE_RELEASED;
 assert(ui_lockscreen_show());lv_obj_update_layout(lv_screen_active());
 touch_state=LV_INDEV_STATE_PRESSED;lv_indev_read(pointer);pump();
 assert(ui_lockscreen_is_active());
 touch_state=LV_INDEV_STATE_RELEASED;lv_indev_read(pointer);pump();pump();
 assert(ui_lockscreen_is_active());lv_tick_inc(400);pump();pump();
 assert(!ui_lockscreen_is_active() && lv_screen_active()==home && app_open_count==0);
 /* Delayed/duplicate evdev contact frames immediately after wake are blocked. */
 touch_state=LV_INDEV_STATE_PRESSED;lv_indev_read(pointer);pump();
 touch_state=LV_INDEV_STATE_RELEASED;lv_indev_read(pointer);pump();
 assert(app_open_count==0);lv_tick_inc(500);pump();
 /* A fresh second touch must still work after the wake touch was consumed. */
 lv_obj_update_layout(home);touch_state=LV_INDEV_STATE_PRESSED;lv_indev_read(pointer);pump();
 touch_state=LV_INDEV_STATE_RELEASED;lv_indev_read(pointer);pump();
 assert(app_open_count==1);
 lv_indev_delete(pointer);

 lv_group_t *group=lv_group_create();lv_obj_t *button=lv_button_create(home);
 lv_obj_add_event_cb(button,count_key,LV_EVENT_KEY,NULL);lv_group_add_obj(group,button);
 lv_indev_t *keypad=lv_indev_create();lv_indev_set_type(keypad,LV_INDEV_TYPE_KEYPAD);
 lv_indev_set_group(keypad,group);lv_indev_set_read_cb(keypad,key_read);
 lv_indev_add_event_cb(keypad,key_wake,LV_EVENT_KEY,NULL);
 for(int i=0;i<4;i++) {
  assert(ui_lockscreen_show());assert(lv_indev_get_group(keypad)!=group);
  key_state=LV_INDEV_STATE_PRESSED;lv_indev_read(keypad);pump();pump();
  assert(ui_lockscreen_is_active() && underlying_keys==0);
  key_state=LV_INDEV_STATE_RELEASED;lv_indev_read(keypad);pump();
  lv_tick_inc(400);pump();assert(!ui_lockscreen_is_active());
  lv_tick_inc(500);pump();assert(lv_indev_get_group(keypad)==group);
 }
 lv_indev_delete(keypad);lv_group_delete(group);lv_obj_delete(button);

 ui_all_apps_show();lv_tick_inc(UI_LOCKSCREEN_IDLE_MS+600);pump();
 assert(!ui_lockscreen_is_active() && !ui_lockscreen_show());ui_all_apps_hide();pump();
 lv_obj_t *modal=lv_obj_create(lv_layer_top());
 lv_tick_inc(UI_LOCKSCREEN_IDLE_MS+600);pump();assert(!ui_lockscreen_show());lv_obj_delete(modal);
 app_setting.init();lv_tick_inc(UI_LOCKSCREEN_IDLE_MS+600);pump();
 assert(!ui_lockscreen_is_active() && !ui_lockscreen_show());
 app_setting.close();lv_obj_t *app=lv_screen_active();lv_screen_load(home);lv_obj_delete(app);
 lv_display_trigger_activity(NULL);pump();
 lv_tick_inc(UI_LOCKSCREEN_IDLE_MS-1000);pump();assert(!ui_lockscreen_is_active());
 lv_tick_inc(1600);pump();assert(ui_lockscreen_is_active());
 /* A theme rebuild while locked must update the return screen before deleting Home. */
 ui_rebuild_home();pump();assert(ui_lockscreen_is_active());
 ui_lockscreen_wake();lv_tick_inc(500);pump();assert(lv_screen_active()!=home);
 assert(find_label(lv_screen_active(),ui_tr(UI_STR_HOME_TALK_MIMI)));
 assert(ui_lockscreen_show());
 modal=lv_obj_create(lv_layer_top());lv_tick_inc(600);pump();
 assert(!ui_lockscreen_is_active());lv_obj_delete(modal);
 puts("PASS lockscreen: real touch/key wake, no click-through, group restore, home-only idle, modal priority, rebuilt return screen");
}
