"""Execute the actual refresh/decision code with only rendering and polling stubbed."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
VALID = dict(busy=False, downloaded=False, phase="idle", candidate=None,
             current_version="1.0", channel="stable", error="")

class UpdateStatus(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.path = Path(cls.tmp.name)
        source = Path(os.environ.get("MIMI_UPDATE_SOURCE", str(ROOT/"apps/lv_port_linux/src/ui/desktop_update.c"))).read_text()
        body = source[source.index("static const char *text_field"):source.index("static void action(")]
        stub = r'''#include <json-c/json.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdarg.h>
typedef void lv_timer_t;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static char response[65536], action_error[256];
static bool worker_busy;
static int states[4], *buttons[4]={&states[0],&states[1],&states[2],&states[3]};
static void *status_label;
#define LV_STATE_DISABLED 1
static void lv_obj_remove_state(int *p,int s) { (void)s; *p=1; }
static void lv_obj_add_state(int *p,int s) { (void)s; *p=0; }
static void lv_label_set_text(void *p,const char *s) { (void)p; (void)s; }
static void lv_label_set_text_fmt(void *p,const char *s,...) { (void)p; (void)s; }
static bool ui_i18n_is_zh(void) {return false;}
static void launch(const char *op) {(void)op;}
'''
        main = r'''int main(void) {
while(fgets(response,sizeof(response),stdin)) {
refresh(NULL); printf("%d%d%d%d\n",states[0],states[1],states[2],states[3]);
} return 0; }'''
        (cls.path/"probe.c").write_text(stub+body+main)
        subprocess.run(["gcc","-Wall","-Wextra","-Werror",str(cls.path/"probe.c"),"-ljson-c","-lpthread","-o",str(cls.path/"probe")],check=True)

    @classmethod
    def tearDownClass(cls): cls.tmp.cleanup()

    def run_states(self, values):
        raw="\n".join(v if isinstance(v,str) else json.dumps(v) for v in values)+"\n"
        r=subprocess.run([str(self.path/"probe")],input=raw,text=True,capture_output=True,check=True)
        return r.stdout.splitlines()

    def test_invalid_state_and_recovery(self):
        cases=[{}, [], True, 42, '"hello"', json.dumps(VALID)+" garbage", json.dumps(VALID)+" {}"]
        for field in VALID:
            missing=VALID.copy(); del missing[field]; cases.append(missing)
        for field,value in [("busy",0),("busy","false"),("downloaded",1),("phase",None),
                            ("downloaded",True),("phase","check\x00ignored"),("candidate",True),("candidate",[]),("candidate",{}),
                            ("candidate",{"manifest":{"version":1}}),("candidate",{"manifest":{"version":""}}),
                            ("current_version",3),("channel",False),("error",[])]:
            invalid=VALID.copy(); invalid[field]=value; cases.append(invalid)
        for value in cases:
            with self.subTest(value=value):
                self.assertEqual(self.run_states([VALID,value,VALID]),["1000","0000","1000"])

    def test_valid_decisions(self):
        candidate={"manifest":{"version":"2.0","sequence":2}}
        cases=[(VALID,"1000"),({**VALID,"candidate":candidate},"1100"),
               ({**VALID,"candidate":candidate,"downloaded":True},"1110"),
               ({**VALID,"busy":True,"phase":"check"},"0001"),
               ({**VALID,"busy":True,"phase":"download"},"0001"),
               ({**VALID,"busy":True,"phase":"install"},"0000"),
               ({**VALID,"extra":{"future":True}},"1000")]
        for value,expected in cases:
            with self.subTest(value=value): self.assertEqual(self.run_states([value]),[expected])
