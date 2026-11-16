#ifndef TFP_FAKE_NATIVE_H
#define TFP_FAKE_NATIVE_H
#include "backend.h"
#include "dispatch_lease.h"
#include "profile.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct Block {uintptr_t at;size_t size;uint8_t data[8192];} Block;
typedef struct Fake {Block blocks[256];unsigned count,reads;uintptr_t denied;} Fake;
static Fake f;
static bool read_fake(void *user,uintptr_t at,void *out,size_t size){
    Fake *x=user;x->reads++;if(x->denied&&at<=x->denied&&size>x->denied-at)return false;
    for(size_t j=0;j<size;j++){
        bool found=false;
        for(unsigned i=x->count;i>0;i--){Block *b=x->blocks+i-1;uintptr_t p=at+j;
            if(p>=b->at&&p-b->at<b->size){((uint8_t *)out)[j]=b->data[p-b->at];found=true;break;}}
        if(!found)return false;
    }return true;
}
static void put(uintptr_t at,const void *data,size_t size){
    for(unsigned i=f.count;i>0;i--){Block *b=f.blocks+i-1;if(at>=b->at&&at-b->at<=b->size&&size<=b->size-(at-b->at)){memcpy(b->data+(at-b->at),data,size);return;}}
    assert(f.count<256&&size<=8192);Block *b=f.blocks+f.count++;b->at=at;b->size=size;memcpy(b->data,data,size);
}
static void n32(uintptr_t at,uint32_t n){put(at,&n,4);}
static void p64(uintptr_t at,uint64_t n){put(at,&n,8);}
static void map(uintptr_t at,uintptr_t buckets,uint32_t key,uint32_t index){
    uint8_t h[20];uint32_t cap=8,empty=UINT32_MAX,mult=3,rows[16];
    memcpy(h,&buckets,8);memcpy(h+8,&cap,4);memcpy(h+12,&empty,4);memcpy(h+16,&mult,4);put(at,h,20);
    memset(rows,255,sizeof rows);uint32_t slot=(key*mult)&7;rows[slot*2]=key;rows[slot*2+1]=index;put(buckets,rows,sizeof rows);
}
static void image(uintptr_t base,uint32_t stamp,uint32_t size){
    uint8_t h[256]={0};uint32_t pe=64,sig=0x4550;uint16_t magic=0x20b;
    h[0]='M';h[1]='Z';memcpy(h+60,&pe,4);memcpy(h+64,&sig,4);memcpy(h+72,&stamp,4);memcpy(h+88,&magic,2);memcpy(h+144,&size,4);put(base,h,sizeof h);
}
static FpMemory setup(void){
    memset(&f,0,sizeof f);FpMemory m={read_fake,&f,0x100000000ULL,0x200000000ULL};
    uintptr_t ctx=0x300000000ULL,world=0x400000000ULL,ctrl=0x500000000ULL,player=0x600000000ULL;
    uintptr_t avatar=0x700000000ULL,ep=0x800000000ULL,wield=0x900000000ULL,weapons=0xa00000000ULL;
    uint32_t local[6]={11,22,101,401,301,1},weapon[6]={33,44,201,501,601,1};
    uint8_t controls[6]={0},flags[24]={0};
    image(m.game,FP_GAME_STAMP,FP_GAME_SIZE);image(m.exe,FP_EXE_STAMP,FP_EXE_SIZE);
    p64(m.game+FP_CAMERA,ctx);p64(ctx,ctx+0x3000);n32(ctx+8,701);
    n32(ctx+0x1f8,0);n32(ctx+0x1fc,1);uint32_t request[5]={4,101,201,401,0};put(ctx+0x200,request,20);
    p64(m.game+FP_WORLD,world);p64(m.game+FP_CONTROLLER,ctrl);put(ctrl,controls,6);
    p64(m.game+FP_PLAYER,player);n32(player+0x88,1);n32(player+0x3a8,301);
    map(world+FP_LIGHT_MAP,world+0x100,301,0);map(world+FP_ENTITY_MAP,world+0x200,201,1);
    put(world+FP_ENTITY_ROWS,local,24);put(world+FP_ENTITY_ROWS+24,weapon,24);
    p64(m.game+FP_AVATAR,avatar);map(avatar+0xf8,avatar+0x300,101,0);n32(avatar+0x6c,1);
    p64(avatar+0x110,ep);put(ep,local,20);put(avatar+FP_AVATAR_FLAGS,flags,24);n32(avatar+FP_AVATAR_RIG,801);
    p64(m.game+FP_WIELD,wield);map(wield+0x30,wield+0x200,101,0);p64(wield+0x48,wield+0x300);
    p64(wield+0x300,ep);p64(wield+0x60,wield+0x400);n32(wield+0x400,201);
    p64(m.game+FP_WEAPONS,weapons);map(weapons+0x30,weapons+0x200,201,0);p64(weapons+0x48,weapons+0x300);
    p64(weapons+0x300,weapons+0x400);put(weapons+0x400,weapon,20);
    return m;
}

static inline void fake_dispatch(FpDispatchLease *lease){
 lease->count=1;lease->slots[0]=(FpDispatchSlot){0x2900000000ULL,0,0x2900010000ULL,true};p64(lease->slots[0].address,lease->slots[0].replacement);
}
#include "scene.h"
static void unit(const FpMemory *m,uint32_t ref,uintptr_t object,uint32_t count,uintptr_t pool){
    uintptr_t registry=0xb00000000ULL,vt=pool+0x100,world=0x400000000ULL;
    p64(m->exe+FP_REGISTRY,registry);n32(registry+0x98,1024);p64(registry+0xa0,registry+0x1000);p64(registry+0x88,registry+0x2000);
    uint8_t generations[1024]={0};put(registry+0x1000,generations,sizeof generations);
    p64(registry+0x2000+(ref&0x3fffff)*8,object);p64(object,vt);n32(object+8,ref);p64(object+0x18,world);
    p64(vt+0xf8,m->exe+0x2bd890);p64(vt+0xc8,m->exe+0x2c3990);p64(vt+0xe8,m->exe+0x2bd870);
    const uint8_t getter[]={0x48,0x8d,0x41,0x60,0xc3};put(m->exe+0x2bd870,getter,sizeof getter);
    n32(object+0x70,count);p64(object+0x80,pool+0x1000);p64(object+0x88,pool+0x3000);p64(object+0x98,pool+0x2000);p64(object+0xa0,pool+0x2800);
    uint32_t zeros[512]={0};put(pool+0x2000,zeros,count*4);put(pool+0x2800,zeros,count*4);
    float matrix[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};put(pool+0x1000,matrix,64);put(pool+0x3000,matrix,64);
}

static uintptr_t live_begin,live_end;
static bool native_fake_read(void *user,uintptr_t at,void *out,size_t size){
    if(at>=live_begin&&at<live_end&&size<=live_end-at){memcpy(out,(const void *)at,size);return true;}
    return read_fake(user,at,out,size);
}
static FpMemory native_setup(void){
    FpMemory m=setup();m.read=native_fake_read;
    unit(&m,401,0xc00000000ULL,107,0xd00000000ULL);
    n32(0xd00000000ULL+0x2800+90*4,0x8c5570c9);n32(0xd00000000ULL+0x2000+90*4,89*65536+1);n32(0xd00000000ULL+0x2800+89*4,0xe9ecca21);
    float head[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,1.7f,1};put(0xd00000000ULL+0x3000+90*64,head,64);
    unit(&m,701,0xe00000000ULL,1,0xf00000000ULL);p64(0xe00000000ULL+0xa0,0);p64(0xe00000000ULL+0x1d0,0);
    p64(0x300003000ULL+0x18,0xe00000000ULL+0x60);n32(0x300003000ULL+0x20,0);
    unit(&m,801,0x1100000000ULL,63,0x1200000000ULL);
    static const struct {uint32_t n,h,l;} nodes[]={
        {0,0x4a182741,0},{1,0x7f30e61c,1},{2,0x32623eb2,0x10001},{3,0x975cebbd,0x20001},
        {4,0xbccf91e5,0x30001},{5,0x9b115563,0x40001},{7,0x6f3bd021,0x50001},{8,0x4fdd3337,0x70001},
        {10,0x03752c98,0x70001},{12,0x1ca2cd66,0xa0001},{13,0x5c0c68d3,0xc0001},
        {30,0x1620b2ce,0x70001},{32,0x31e34dac,0x1e0001},{34,0x248004f5,0x200001},{50,0x60ed8c39,0x40001}
    };
    for(unsigned i=0;i<sizeof nodes/sizeof nodes[0];i++){n32(0x1200000000ULL+0x2800+nodes[i].n*4,nodes[i].h);n32(0x1200000000ULL+0x2000+nodes[i].n*4,nodes[i].l);}
    float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};put(0x1200000000ULL+0x3000+4*64,identity,64);put(0x1200000000ULL+0x3000+50*64,head,64);
    unit(&m,501,0x2300000000ULL,1,0x2400000000ULL);p64(0x2300000000ULL+0x358,UINT64_C(0x0102030405060708));n32(0x300000000ULL+0x218,1);
    p64(m.game+0x3326348,0x1300000000ULL);float dt=.016f;put(0x1300000000ULL,&dt,4);float raw[3]={0};put(0x700000000ULL+0xa7c24,raw,12);
    return m;
}
#endif
