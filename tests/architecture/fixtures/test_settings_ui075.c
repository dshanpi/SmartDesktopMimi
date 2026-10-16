#define _GNU_SOURCE
#include "settings.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(void)
{
    char dir[]="/tmp/mimi-ui075-settings-XXXXXX",path[256];
    assert(mkdtemp(dir));snprintf(path,sizeof(path),"%s/settings",dir);
    assert(!setenv("LV_SETTINGS_FILE",path,1));
    sys_settings_t *s=sys_settings_get();assert(s->brightness==20 && !s->hdmi_enabled);
    sys_settings_init();s->brightness=73;s->volume=42;s->hdmi_enabled=true;
    assert(sys_settings_save_checked()==0);
    s->brightness=20;s->volume=0;s->hdmi_enabled=false;
    sys_settings_init();assert(s->brightness==73 && s->volume==42 && s->hdmi_enabled);
    assert(!setenv("LV_SETTINGS_FILE","/missing-ui075-settings/settings",1));
    assert(sys_settings_save_checked()<0);
    assert(!setenv("LV_SETTINGS_FILE",path,1));sys_settings_init();
    assert(s->brightness==73 && s->volume==42 && s->hdmi_enabled);
    assert(!unlink(path));assert(!rmdir(dir));
    puts("PASS UI075 actual settings: first boot 20%/HDMI off; saved choices retained; failed write reported; disk data retained");
}
