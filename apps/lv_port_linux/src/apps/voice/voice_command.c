#include "voice_command.h"
#include <stddef.h>
#include <string.h>
#include <ctype.h>
bool mimi_command_parse(const char * text, mimi_command_t * command)
{
    if(!command) return false;
    *command = MIMI_COMMAND_NONE;
    if(!text) return false;
    size_t n = strnlen(text, 384);
    if(!n || n >= 384) return false;
    while(n && isspace((unsigned char)*text)) {
        text++;
        n--;
    }
    while(n && isspace((unsigned char)text[n - 1])) n--;
    if(n >= 3 && (!memcmp(text + n - 3, "。", 3) || !memcmp(text + n - 3, "！", 3)))
        n -= 3;
    else if(n && (text[n - 1] == '.' || text[n - 1] == '!'))
        n--;
    char normalized[384];
    for(size_t i = 0; i < n; i++) {
        unsigned char c = text[i];
        if(c < 32 || c == 127) return false;
        normalized[i] = c < 128 ? (char)tolower(c) : (char)c;
    }
    normalized[n] = 0;
    static const struct
    {
        const char * text;
        mimi_command_t command;
    } vocabulary[] = {{"返回首页", MIMI_COMMAND_HOME},
                      {"回到首页", MIMI_COMMAND_HOME},
                      {"go home", MIMI_COMMAND_HOME},
                      {"打开应用抽屉", MIMI_COMMAND_DRAWER},
                      {"打开应用列表", MIMI_COMMAND_DRAWER},
                      {"open apps", MIMI_COMMAND_DRAWER},
                      {"打开设置", MIMI_COMMAND_SETTINGS},
                      {"open settings", MIMI_COMMAND_SETTINGS},
                      {"打开无线网络", MIMI_COMMAND_WIFI},
                      {"打开wifi", MIMI_COMMAND_WIFI},
                      {"打开wi-fi", MIMI_COMMAND_WIFI},
                      {"open wifi", MIMI_COMMAND_WIFI},
                      {"open wi-fi", MIMI_COMMAND_WIFI},
                      {"打开蓝牙", MIMI_COMMAND_BLUETOOTH},
                      {"open bluetooth", MIMI_COMMAND_BLUETOOTH},
                      {"打开视频", MIMI_COMMAND_VIDEO},
                      {"open video", MIMI_COMMAND_VIDEO},
                      {"打开设备配对", MIMI_COMMAND_PAIRING},
                      {"open pairing", MIMI_COMMAND_PAIRING},
                      {"打开涂鸦智家", MIMI_COMMAND_IOT},
                      {"open smart home", MIMI_COMMAND_IOT}};
    for(size_t i = 0; i < sizeof(vocabulary) / sizeof(vocabulary[0]); i++)
        if(!strcmp(normalized, vocabulary[i].text)) {
            *command = vocabulary[i].command;
            return true;
        }
    return false;
}
