#include "../src/fov_policy.c"
#include "fake_native.h"
#include <assert.h>
static float received;
static void collect(void *camera,float value){(void)camera;received=value;}
int main(void){
    FpMemory m=native_setup();FpSnapshot s;char reason[160];assert(fp_snapshot(&m,true,&s,reason,sizeof reason));
    FpFovPolicy p;fp_fov_policy_init(&p);p.memory=m;p.origin=s;p.lens.blend_ms=0;
    fake_dispatch(&p.lease);atomic_store(&p.lease.armed,true);atomic_store(&active,&p);atomic_store(&native_setter,collect);
    p64(m.game+0x3326340,0x1400000000ULL);float degrees=90;put(0x1400000000ULL+0xac408,&degrees,4);n32(s.request,6);
    lens_call(m.game+0x127a120,(void *)s.camera,.4f);assert(fabsf(received-1.57079632679f)<.000001f);
    lens_call(m.game+0x127a120,(void *)s.camera,.2f);assert(fabsf(received-1.57079632679f)<.000001f);
    uint8_t controls[6]={0,0,0,0,1,0};put(s.controller,controls,6);lens_call(m.game+0x127a120,(void *)s.camera,.2f);assert(received==.2f);
    controls[4]=0;put(s.controller,controls,6);lens_call(m.game+0x127a121,(void *)s.camera,.4f);assert(received==.4f);
    n32(s.request,4);lens_call(m.game+0x127a120,(void *)s.camera,.7f);assert(received==.7f);
    atomic_store(&active,NULL);puts("C FOV policy: zoom-independent hip lens, native ADS magnification, exact callers and normal-camera pass-through passed");return 0;
}
