import json
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]

class CommandReplyTests(unittest.TestCase):
    def test_strict_reply_and_transaction(self):
        good = dict(runtime='tuya', command_result='exit_free_chat', request_id='a-1', result=0)
        valid = [json.dumps(good), json.dumps(dict(reversed(list(good.items())))),
                 json.dumps(dict(good, result=-7)), json.dumps(dict(good, command_result='enter_free_chat'))]
        bad = ['null', '[]', '{}', json.dumps(good) + ' trailing',
               json.dumps(good) + json.dumps(good),
               json.dumps(good)[:-1] + ', "result":-7}',
               json.dumps(good)[:-1] + ', "re\\u0073ult":-7}',
               json.dumps(dict(runtime='tuya', mode='wakeup', state='idle'))]
        for field, values in {
            'runtime': [None, 1, 'other', 'tuya\0suffix'],
            'command_result': [None, 'unknown', 'exit_free_chat\0x', {}],
            'request_id': [None, '', 'x'*65, 'a b', 'a\0b', []],
            'result': [None, False, 0.0, '0', 2147483648, -2147483649, {}],
        }.items():
            bad += [json.dumps(dict(good, **{field: value})) for value in values]
        lines = ['#include <assert.h>', '#include "integrations/tuyaopen/protocol/ai_command_reply.h"',
                 'int main(void) { mimi_ai_reply reply; const char *data;']
        for text in valid:
            lines += ['data=' + json.dumps(text) + ';',
                      'assert(mimi_ai_reply_parse(data, strlen(data), &reply));']
        for text in bad:
            lines += ['data=' + json.dumps(text) + ';',
                      'memset(&reply, 0x5a, sizeof(reply)); mimi_ai_reply before = reply;' if text == bad[0] else
                      'memset(&reply, 0x5a, sizeof(reply)); before = reply;',
                      'assert(!mimi_ai_reply_parse(data, strlen(data), &reply));',
                      'assert(!memcmp(&reply, &before, sizeof(reply)));']
        lines += ['data=' + json.dumps(json.dumps(good)) + ';',
                  'for(size_t n=0;n<strlen(data);n++) assert(!mimi_ai_reply_parse(data,n,&reply));',
                  'assert(!mimi_ai_reply_parse(data,strlen(data)+1,&reply));',
                  'assert(mimi_ai_reply_parse(data,strlen(data),&reply));',
                  'mimi_ai_transaction tx={0};',
                  'assert(mimi_ai_tx_begin(&tx,MIMI_AI_EXIT,"a-1",100,30));',
                  'assert(mimi_ai_tx_accept(&tx,reply.command,reply.id,reply.result,101));',
                  'assert(tx.state==MIMI_AI_TX_SUCCEEDED);', 'return 0; }']
        with tempfile.TemporaryDirectory() as tmp:
            source = pathlib.Path(tmp) / 'reply.c'
            binary = pathlib.Path(tmp) / 'reply'
            source.write_text('\n'.join(lines))
            flags = subprocess.check_output(['pkg-config', '--cflags', '--libs', 'json-c'], text=True).split()
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                            '-fno-sanitize-recover=all', '-I', str(ROOT), str(source), '-o', str(binary)] + flags,
                           check=True)
            subprocess.run([str(binary)], check=True, timeout=10)

if __name__ == '__main__':
    unittest.main()
