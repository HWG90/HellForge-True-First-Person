#include "fake_native.h"
#include "feature_contract.h"
#include "markers.h"
static unsigned writes;
static bool show(void *unused,uintptr_t widget,bool value){(void)unused;uint32_t bits=0;assert(read_fake(&f,widget,&bits,4));n32(widget,value?bits|16:bits&~16u);writes++;return true;}
int main(void){
 FpMemory m=native_setup();for(unsigned i=0;i<sizeof fp_feature_sites/sizeof *fp_feature_sites;i++){
  const FpFeatureSite *s=fp_feature_sites+i;if(s->feature!=FP_FEATURE_MARKERS)continue;uint8_t b[128];unsigned n=0;
  for(;s->hex[n*2];n++){unsigned x;assert(sscanf(s->hex+n*2,"%2x",&x)==1);b[n]=(uint8_t)x;}put((s->exe?m.exe:m.game)+s->rva,b,n);
 }
 uintptr_t root=0x1500000000ULL,c=root+0x24e340+0x19aaf8,ctx=root+0x500000;
 uint8_t on=1;p64(m.game+0x346d518,root);put(root+0x24e334,&on,1);p64(c+0xf0,root+0x820);p64(c+0xf8,ctx);put(ctx,&on,1);p64(ctx+16,0x400000000ULL);
 for(unsigned i=0;i<2;i++){uintptr_t widget=c+0x110*(i+1);p64(widget+0xf0,c);p64(widget+0xf8,ctx);n32(widget,0x111);}
 FpMarkerLease l={0};assert(fp_markers_update(&l,&m,true,show,NULL));assert(writes==2&&l.held[0]&&l.held[1]);
 assert(fp_markers_update(&l,&m,true,show,NULL));assert(writes==2);assert(fp_markers_update(&l,&m,false,show,NULL));assert(writes==4&&!l.held[0]&&!l.held[1]);
 n32(c+0x220,0x101);assert(fp_markers_update(&l,&m,true,show,NULL));assert(writes==5);assert(fp_markers_update(&l,&m,false,show,NULL));assert(writes==6);uint32_t bits;assert(fp_u32(&m,c+0x220,&bits)&&!(bits&16));
 puts("C aim markers: both groups suppressed during aim, hip restored, existing hidden bits preserved, repeat writes avoided");return 0;
}
