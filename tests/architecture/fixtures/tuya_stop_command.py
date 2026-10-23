from pathlib import Path
import subprocess, sys
root = Path(__file__).resolve().parents[3]
tmp = Path(sys.argv[1])
source = (root / "third_party/TuyaOpen/apps/tuya.ai/your_chat_bot/src/app_chat_bot.c").read_text()
start = source.index("/* Command IDs")
end = source.index("static void __main_ui_cmd_thread(", start)
template = Path(__file__).with_name("tuya_command_template.c").read_text()
(tmp / "command.c").write_text(template.replace("/* PRODUCTION_COMMAND_HANDLER */", source[start:end]))
cjson = root / "third_party/TuyaOpen/src/libcjson/cJSON"
subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                "-fno-sanitize-recover=all", "-I", str(cjson), str(tmp / "command.c"),
                str(cjson / "cJSON.c"), "-lm", "-o", str(tmp / "command")], check=True)
subprocess.run([str(tmp / "command")], check=True)
