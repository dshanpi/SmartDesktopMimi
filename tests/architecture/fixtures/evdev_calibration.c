#include <assert.h>
#include <stdarg.h>
#include <linux/input.h>
#include <sys/ioctl.h>
static int mode,calls;
int fixture_ioctl(int fd,unsigned long request,...){
 (void)fd;va_list args;va_start(args,request);struct input_absinfo *r=va_arg(args,struct input_absinfo*);va_end(args);calls++;
 int axis=_IOC_NR(request)-0x40;
 r->minimum=0;r->maximum=0;
 if(mode==0 && axis==ABS_MT_POSITION_X)r->maximum=1920;
 if(mode==1 && axis==ABS_X){r->minimum=10;r->maximum=1010;}
 if(mode==2)return -1;
 return 0;
}
#define ioctl fixture_ioctl
#include "src/lib/indev_backends/evdev.c"
#undef ioctl
static void test_live_panel(void) {
 struct input_absinfo x={.maximum=1920},y={.maximum=1200};
 const char *config="==== GT9XX config init value====\n0x46 0x80 0x07 0xB0 0x04\n"
                    "==== GT9XX config real value====\n0x63 0x00 0x04 0x00 0x03\n";
 assert(read_gt9xx_panel_range(config,1024,768,&x,&y));
 assert(x.maximum==1023 && y.maximum==767 && x.minimum==0 && y.minimum==0);
 assert(835*1023/x.maximum==835 && 590*767/y.maximum==590);
 assert(!read_gt9xx_panel_range(config,768,1024,&x,&y));
 const char *bad[]={"==== GT9XX config init value====\n0x63 0x00 0x04 0x00 0x03\n",
  "==== GT9XX config real value====\n0x63 0x00", "==== GT9XX config real value====\n0x63 0x100 0x04 0x00 0x03\n"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++)
  assert(!read_gt9xx_panel_range(bad[i],1024,768,&x,&y));
}
int main(){test_live_panel();struct input_absinfo r;
 mode=0;calls=0;assert(read_pointer_range(0,ABS_X,ABS_MT_POSITION_X,&r));assert(r.maximum==1920&&calls==2);
 mode=1;calls=0;assert(read_pointer_range(0,ABS_X,ABS_MT_POSITION_X,&r));assert(r.minimum==10&&r.maximum==1010&&calls==1);
 mode=2;assert(!read_pointer_range(0,ABS_X,ABS_MT_POSITION_X,&r));
 mode=3;assert(!read_pointer_range(0,ABS_X,ABS_MT_POSITION_X,&r));
 puts("PASS: zero-range fallback, valid single-axis range, failed ioctl, no usable range");return 0;}
