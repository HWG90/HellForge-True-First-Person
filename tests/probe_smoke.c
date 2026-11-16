#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "hellforge_mod.h"
static unsigned writes,hooks,reads,events,ticks,logs;
static HfEventCallback loaded;
static HfTickCallback tick;
static const char *build="25480438";
static const char *build_id(void){return build;}
static void log_line(HfMod *mod,const char *text,...){(void)mod;(void)text;logs++;}
static int on(HfMod *mod,HfEvent kind,HfEventCallback callback,void *user){(void)mod;(void)user;assert(kind==HF_EVENT_GAME_LOADED);loaded=callback;events++;return 0;}
static uintptr_t base(const char *name,size_t *size){(void)name;*size=0x4744000;return 0x100000000ULL;}
static int read_memory(uintptr_t at,void *out,size_t size){(void)at;(void)out;(void)size;reads++;return 1;}
static int write_memory(uintptr_t at,const void *data,size_t size){(void)at;(void)data;(void)size;writes++;return 1;}
static int hook(HfMod *mod,uintptr_t at,void *detour,void **original){(void)mod;(void)at;(void)detour;(void)original;hooks++;return 1;}
static int on_tick(HfMod *mod,HfTickCallback callback,void *user){(void)mod;(void)user;tick=callback;ticks++;return 1;}
int main(int argc,char **argv){
    assert(argc==2);HMODULE dll=LoadLibraryA(argv[1]);assert(dll);
    typedef int (*Init)(const HfApi *,HfMod *);
    Init init=(Init)(void *)GetProcAddress(dll,"hellforge_mod_init");assert(init);
    HfApi api={0};api.size=sizeof api;api.version=16;api.build_id=build_id;api.log=log_line;api.on=on;
    api.module_base=base;api.read=read_memory;api.write=write_memory;api.hook=hook;api.on_game_thread=on_tick;
    HfMod *mod=(HfMod *)(uintptr_t)0x1234;
    build="26251122";assert(init(&api,mod)==1&&events==0);build="25480438";
    api.version=15;assert(init(&api,mod)==1&&events==0);api.version=16;
    assert(init(&api,mod)==0&&events==1&&loaded&&logs==1);
    loaded(mod,HF_EVENT_GAME_LOADED,NULL);assert(ticks==1&&tick);
    tick(mod,.016f,NULL);assert(reads==1&&writes==0&&hooks==0);
    printf("C probe DLL: wrong-build refusal, SDK refusal, lifecycle and read-only tick passed\n");
    return 0;
}
