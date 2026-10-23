"""Check the actual catalog scanner name storage against schema Unicode bounds."""
import json
import copy
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
class CatalogNames(unittest.TestCase):
 def test_unicode_name_storage(self):
  source=(ROOT/'apps/lv_port_linux/src/apps/user/user_app.c').read_text()
  declarations=source[source.index('typedef struct {'):source.index('static lv_obj_t *screen;')]
  scanner=source[source.index('enum {'):source.index('static void set_plain_container(')]
  prefix=r'''#define _GNU_SOURCE
#include <assert.h>
#include <dirent.h>
#include <json-c/json.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#define USER_APP_MAX 16
#define USER_APP_JSON_MAX (1024*1024)
static struct {const char *user_apps_root;} config;
#define mimi_user_app_config_get() (&config)
static bool ui_i18n_is_zh(void){return true;}
'''
  globals='static user_app_entry_t entries[USER_APP_MAX];static size_t entry_count;\n'
  main=r'''int main(int argc,char **argv){assert(argc==4);config.user_apps_root=argv[1];scan_apps();
assert(entry_count==(size_t)atoi(argv[3]));if(entry_count){assert(!strcmp(entries[0].name,argv[2]));assert(entries[0].permissions==0);assert(strstr(entries[0].entry,"/ui/main.json"));}return 0;}
'''
  with tempfile.TemporaryDirectory() as tmp:
   d=Path(tmp);c=d/'test.c';c.write_text(prefix+declarations+globals+scanner+main)
   binary=d/'test';subprocess.run(['gcc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(c),'-o',str(binary),'-ljson-c'],check=True)
   app=d/'apps/com.test';(app/'ui').mkdir(parents=True);(app/'ui/main.json').write_text('{}');(app/'ui/other.json').write_text('{}')
   cases=[('A'+'中'*16,False,1),('AB'+'😀'*12,False,1),('A'*48,False,1),('中'*48,False,1),('😀'*48,False,1),('中'*48,True,1),('A'*193,False,0)]
   for name,localized,count in cases:
    with self.subTest(name=name,localized=localized):
     manifest=dict(id='com.test',name='Base' if localized else name,ui=dict(entry='ui/main.json',presentation='embedded'),permissions=[])
     if localized:manifest['localizations']={'zh-CN':{'name':name}}
     (app/'manifest.json').write_text(json.dumps(manifest))
     subprocess.run([str(binary),str(d/'apps'),name,str(count)],check=True)
   base=dict(id='com.test',name='Base',ui=dict(entry='ui/main.json',presentation='embedded'),permissions=[])
   variants=[]
   for field,value in [('id','com.test\0suffix'),('name','Base\0suffix')]:
    m=copy.deepcopy(base);m[field]=value;variants.append((field,m,0))
   for field,value in [('entry','ui/main.json\0suffix'),('presentation','embedded\0suffix')]:
    m=copy.deepcopy(base);m['ui'][field]=value;variants.append((field,m,0))
   m=copy.deepcopy(base);m['permissions']=['capture.snapshot\0suffix'];variants.append(('permission',m,1))
   for field,value in [('name','Hidden\0suffix'),('uiEntry','ui/other.json\0suffix')]:
    m=copy.deepcopy(base);m['localizations']={'zh-CN':{field:value}};variants.append(('localized-'+field,m,1))
   for label,manifest,count in variants:
    with self.subTest(field=label):
     (app/'manifest.json').write_text(json.dumps(manifest))
     subprocess.run([str(binary),str(d/'apps'),'Base',str(count)],check=True)
   app.rename(app.parent/'.hidden')
   long_id='a'*97;long_app=app.parent/long_id;(long_app/'ui').mkdir(parents=True)
   (long_app/'ui/main.json').write_text('{}')
   base['id']=long_id;(long_app/'manifest.json').write_text(json.dumps(base))
   subprocess.run([str(binary),str(d/'apps'),'Base','0'],check=True)
if __name__=='__main__':unittest.main()
