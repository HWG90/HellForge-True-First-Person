#include "backend.h"
#include "profile.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct Block {uintptr_t at;size_t size;uint8_t data[8192];} Block;
typedef struct Fake {Block blocks[128];unsigned count,reads;uintptr_t denied;} Fake;
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
    assert(f.count<128&&size<=8192);Block *b=f.blocks+f.count++;b->at=at;b->size=size;memcpy(b->data,data,size);
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
int main(void){
    FpMemory m=setup();FpSnapshot s,a;char why[160];uint32_t index=0;unsigned cases=0;
    assert(fp_image(&m,m.game,FP_GAME_STAMP,FP_GAME_SIZE));assert(!fp_image(&m,m.game,FP_GAME_STAMP+1,FP_GAME_SIZE));cases++;
    assert(fp_snapshot(&m,true,&s,why,sizeof why)&&s.safe&&s.avatar==101&&s.weapon==201&&s.body==401&&s.weapon_unit==501);cases++;
    a=s;assert(fp_same_player(&s,&a));a.descriptor[4]^=1;assert(!fp_same_player(&s,&a));cases++;
    n32(s.context+0x200,6);assert(fp_snapshot(&m,true,&s,why,sizeof why)&&s.kind==6);cases++;
    n32(s.context+0x208,999);assert(!fp_snapshot(&m,true,&s,why,sizeof why));cases++;
    assert(fp_snapshot(&m,false,&s,why,sizeof why));cases++;
    m=setup();n32(0x300000000ULL+0x1fc,32);assert(!fp_snapshot(&m,false,&s,why,sizeof why));cases++;
    m=setup();f.denied=0x800000000ULL;assert(!fp_snapshot(&m,false,&s,why,sizeof why));cases++;
    m=setup();n32(0x700000000ULL+0x6c,9);assert(!fp_snapshot(&m,false,&s,why,sizeof why));cases++;
    m=setup();n32(0x500000000ULL,2);assert(fp_snapshot(&m,true,&s,why,sizeof why)&&!s.safe);cases++;
    m=setup();n32(0x400000000ULL+0x100+((301*3)&7)*8+4,1048576);assert(!fp_snapshot(&m,false,&s,why,sizeof why));cases++;
    m=setup();assert(fp_map(&m,0x700000000ULL+0xf8,101,8,&index)&&index==0);assert(!fp_map(&m,0x700000000ULL+0xf8,102,8,&index));cases++;
    n32(0x700000000ULL+0x100,7);assert(!fp_map(&m,0x700000000ULL+0xf8,101,8,&index));cases++;
    unsigned before=f.reads;uint8_t byte;assert(!fp_read(&m,1,&byte,1));assert(!fp_read(&m,0x10000,&byte,0));assert(!fp_read(&m,0x800000000000ULL,&byte,1));assert(f.reads==before);cases++;
    printf("C backend: %u ownership/bounds cases passed\n",cases);return 0;
}
