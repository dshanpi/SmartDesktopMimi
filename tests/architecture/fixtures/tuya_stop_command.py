from pathlib import Path
import subprocess, sys
root = Path(__file__).resolve().parents[3]
tmp = Path(sys.argv[1])
source = (root / "third_party/TuyaOpen/apps/tuya.ai/your_chat_bot/src/app_chat_bot.c").read_text()
start = source.index("/* Command IDs")
end = source.index("static void __main_ui_cmd_thread(", start)
template = Path(__file__).with_name("tuya_command_template.c").read_text()
template = '#include "integrations/tuyaopen/protocol/ai_command_reply.h"' + chr(10) + template
template = template.replace('#define TRUE 1', '#ifndef TRUE'+chr(10)+'#define TRUE 1'+chr(10)+'#endif').replace('#define FALSE 0', '#ifndef FALSE'+chr(10)+'#define FALSE 0'+chr(10)+'#endif')
template = template.replace('snprintf(response,sizeof(response),"%s",s);}',
    'snprintf(response,sizeof(response),"%s",s); mimi_ai_reply parsed; '
    'assert(mimi_ai_reply_parse(s,strlen(s),&parsed)); assert(parsed.result==outcome);}')
(tmp / "command.c").write_text(template.replace("/* PRODUCTION_COMMAND_HANDLER */", source[start:end]))
cjson = root / "third_party/TuyaOpen/src/libcjson/cJSON"
subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                "-fno-sanitize-recover=all", "-I", str(root), "-I", str(cjson), str(tmp / "command.c"),
                str(cjson / "cJSON.c"), "-lm", "-ljson-c", "-o", str(tmp / "command")], check=True)
subprocess.run([str(tmp / "command")], check=True)
