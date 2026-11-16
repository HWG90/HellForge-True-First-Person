#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/engine.c"
static int mode,calls,closes,pcalls;
static HMODULE lua;
static void log_message(HfMod* mod,const char* format,...){(void)mod;(void)format;}
static int hook(HfMod* mod,uintptr_t target,void* detour,void** original){
 (void)mod;(void)detour;calls++;
 if(target==(uintptr_t)(void*)GetProcAddress(lua,"lua_close")){closes++;if(mode==1)return 1;}
 else {assert(target==(uintptr_t)(void*)GetProcAddress(lua,"lua_pcall"));assert(s_closeInstalled);pcalls++;if(mode==2)return 1;}
 *original=(void*)target;return 0;
}
int main(int argc,char** argv){
 assert(argc==3);mode=atoi(argv[2]);lua=LoadLibraryA(argv[1]);assert(lua);HfApi hf={0};hf.hook=hook;hf.log=log_message;
 bool ready=eng_init(&hf,NULL);
 if(mode==3){assert(!ready&&!calls&&!s_captureEnabled&&!s_closeInstalled&&!s_pcallInstalled);}
 else if(mode==1){assert(!ready&&closes==1&&!pcalls&&!s_captureEnabled&&!s_closeInstalled);}
 else if(mode==2){assert(!ready&&closes==1&&pcalls==1&&!s_captureEnabled&&s_closeInstalled&&!s_pcallInstalled);mode=0;assert(eng_init(&hf,NULL));assert(closes==1&&pcalls==2&&s_captureEnabled);}
 else {assert(ready&&closes==1&&pcalls==1&&s_captureEnabled);int before=calls;assert(eng_init(&hf,NULL)&&calls==before);}
 puts("C engine: capture requires lifetime tracking; missing/rejected close refuses capture, partial pcall failure retries without duplicate hooks");return 0;
}
