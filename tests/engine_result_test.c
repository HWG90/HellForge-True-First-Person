/* Signature decoder fixture; no game, hooks or real Lua VM. */
#include <assert.h>
#include <stdio.h>
#include "../src/engine.c"
static int top=1,types[5],unrefs,refs;
static double values[5];
static int get_top(lua_State* l){(void)l;return top;}
static void set_top(lua_State* l,int n){(void)l;top=n;}
static int check_stack(lua_State* l,int n){(void)l;(void)n;return 1;}
static int call(lua_State* l,int a,int n,int e){(void)l;(void)a;(void)e;top=n;return 0;}
static int type_at(lua_State* l,int at){(void)l;return types[at];}
static double number_at(lua_State* l,int at){(void)l;return values[at];}
static void push_value(lua_State* l,int at){(void)l;(void)at;top++;}
static int reference(lua_State* l,int registry){(void)l;(void)registry;top--;refs++;return refs;}
static void unreference(lua_State* l,int registry,int ref){(void)l;(void)registry;assert(ref>0);unrefs++;}
static bool decode(const char* sig,...){top=1;va_list ap;va_start(ap,sig);bool ok=CallV((lua_State*)1,"fixture",sig,0,ap);va_end(ap);assert(top==0);return ok;}
int main(void){
 L_.gettop=get_top;L_.settop=set_top;L_.checkstack=check_stack;L_.pcall=call;L_.type=type_at;L_.tonumber=number_at;L_.pushvalue=push_value;L_.ref=reference;L_.unref=unreference;
 double n=7;int i=7;EngRef r=0;
 types[1]=LUA_TNIL;assert(!decode(">n",&n)&&n==7);assert(!decode(">i",&i)&&i==7);
 types[1]=LUA_TNUMBER;values[1]=0;assert(decode(">n",&n)&&n==0);assert(decode(">i",&i)&&i==0);
 values[1]=NAN;assert(!decode(">n",&n));values[1]=INFINITY;assert(!decode(">n",&n));assert(!decode(">i",&i));
 values[1]=(double)INT_MAX+1;assert(!decode(">i",&i));values[1]=(double)INT_MIN-1;assert(!decode(">i",&i));values[1]=1.5;assert(!decode(">i",&i));
 values[1]=INT_MAX;assert(decode(">i",&i)&&i==INT_MAX);values[1]=INT_MIN;assert(decode(">i",&i)&&i==INT_MIN);
 types[1]=LUA_TSTRING;assert(!decode(">n",&n));types[1]=LUA_TTABLE;types[2]=LUA_TNIL;assert(!decode(">rn",&r,&n)&&r==0&&refs==1&&unrefs==1);
 types[1]=LUA_TNIL;assert(decode(">r",&r)&&r==0);
 puts("C engine: engine result decoding rejects missing/non-finite numbers and invalid integers; partial references are released");return 0;
}
