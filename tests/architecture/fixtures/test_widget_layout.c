#define _GNU_SOURCE
#include "desktop_widget_layout.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static void put(const char *p, const char *s) {
  FILE *f = fopen(p, "w");
  assert(f);
  assert(fputs(s, f) >= 0);
  assert(!fclose(f));
}
int main(void) {
  char dir[] = "/tmp/mimi-layout-test-XXXXXX", path[256], other[512];
  assert(mkdtemp(dir));
  snprintf(path, sizeof(path), "%s/layout.json", dir);
  snprintf(other, sizeof(other), "%s/other", dir);
  mimi_widget_layout l, old, readback;
  assert(!mimi_widget_layout_load(path, &l));
  assert(mimi_widget_layout_valid(&l));
  old = l;
  assert(mimi_widget_layout_set(&l, 4, MIMI_WIDGET_ENVIRONMENT) < 0);
  assert(!memcmp(&l, &old, sizeof(l)));
  assert(mimi_widget_layout_set(&l, 0, MIMI_WIDGET_NETWORK) < 0);
  assert(mimi_widget_layout_swap(&l, 0, 2) < 0);
  assert(!memcmp(&l, &old, sizeof(l)));
  assert(!mimi_widget_layout_swap(&l, 0, 1));
  assert(l.slots[0] == MIMI_WIDGET_HDMI);
  assert(!mimi_widget_layout_set(&l, 3, MIMI_WIDGET_EMPTY));
  assert(!mimi_widget_layout_set(&l, 3, MIMI_WIDGET_NETWORK));
  assert(!mimi_widget_layout_save(path, &l));
  assert(l.revision == 1);
  assert(!mimi_widget_layout_load(path, &readback));
  assert(!memcmp(&readback, &l, sizeof(l)));
  struct stat st;
  assert(!stat(path, &st));
  assert((st.st_mode & 0777) == 0600);
  assert(mimi_widget_layout_save(path, &old) == MIMI_LAYOUT_CONFLICT);
  old = l;
  assert(!mimi_widget_layout_set(&l, 2, MIMI_WIDGET_EMPTY));
  l = old; /* cancel draft */
  assert(!mimi_widget_layout_load(path, &readback));
  assert(!memcmp(&readback, &l, sizeof(l)));
  assert(mimi_widget_layout_save("/missing-mimi-dir/layout.json", &l) ==
         MIMI_LAYOUT_IO);
  assert(l.revision == 1);
  const char *bad[] = {
      "garbage",
      "{}",
      "{\"schema\":2,\"revision\":1,\"slots\":[\"assistant\",\"hdmi\","
      "\"environment\",\"iot\",\"connection\"]}",
      "{\"schema\":1,\"revision\":1,\"slots\":[\"assistant\",\"assistant\","
      "\"environment\",\"iot\",\"connection\"]}",
      "{\"schema\":1,\"revision\":-1,\"slots\":[\"assistant\",\"hdmi\","
      "\"environment\",\"iot\",\"connection\"]}",
      "{\"schema\":1,\"revision\":1,\"slots\":[\"assistant\",\"hdmi\","
      "\"environment\",\"iot\",\"connection\"]} junk"};
  for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
    put(path, bad[i]);
    readback = l;
    assert(mimi_widget_layout_load(path, &readback) < 0);
    assert(!memcmp(&readback, &l, sizeof(l)));
    assert(mimi_widget_layout_save(path, &l) < 0);
    FILE *f = fopen(path, "r");
    char buf[512] = {0};
    assert(f);
    fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    assert(!strcmp(buf, bad[i]));
  }
  unlink(path);
  put(other, "untouched");
  assert(!symlink(other, path));
  assert(mimi_widget_layout_load(path, &l) == MIMI_LAYOUT_IO);
  assert(mimi_widget_layout_save(path, &l) < 0);
  unlink(path);
  unlink(other);
  snprintf(other, sizeof(other), "%s.lock", path);
  unlink(other);
  assert(!rmdir(dir));
  puts("PASS UI075: slot compatibility, uniqueness, swap, cancel, atomic "
       "roundtrip, revision conflict, private permissions, failed save, "
       "corrupt/future/trailing data preservation, symlink rejection");
}
