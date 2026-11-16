#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "hellforge_mod.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
static unsigned callbacks;static const char *build(void){return "wrong";}static const char *target(void){return "25480438";}
static void log_message(HfMod *m,const char *format,...){(void)m;(void)format;}
static int on(HfMod *m,HfEvent e,HfEventCallback cb,void *u){(void)m;(void)e;(void)cb;(void)u;callbacks++;return 0;}
int main(int argc,char **argv){assert(argc==2);HMODULE mod=LoadLibraryA(argv[1]);assert(mod);int (*init)(const HfApi *,HfMod *)=(void *)GetProcAddress(mod,"hellforge_mod_init");assert(init);
 HfApi api={0};api.version=16;api.size=sizeof api;api.build_id=build;api.log=log_message;api.on=on;assert(init(&api,NULL)==1&&callbacks==0);
 api.version=15;api.build_id=target;assert(init(&api,NULL)==1&&callbacks==0);api.version=16;assert(init(&api,NULL)==0&&callbacks==2);
 puts("C DLL: Windows loader, export, wrong-build/SDK refusal, disabled-by-default initialization and lifecycle registration passed");return 0;
}
