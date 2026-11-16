#ifndef TFP_BACKEND_H
#define TFP_BACKEND_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef bool (*FpRead)(void *user,uintptr_t address,void *out,size_t size);
typedef struct FpMemory {FpRead read;void *user;uintptr_t game,exe;} FpMemory;
typedef struct FpSnapshot {
    uintptr_t context,camera,world,controller,player,avatar_owner,avatar_descriptor;
    uintptr_t request,weapon_descriptor;
    uint32_t head,tail,kind,request_avatar,request_weapon,avatar_index,avatar,body;
    uint32_t weapon,weapon_unit,viewmodel,camera_unit,local_entity,mask;
    uint8_t descriptor[20],weapon_bytes[20],flags[24],controls[6];
    bool safe,local_request,aiming,full_ads,shoulder_aim,reloading,seated,weapon_ready;
} FpSnapshot;
typedef struct FpProof {unsigned matched,unreadable,mismatched;char first[80];} FpProof;
typedef struct FpWeaponRecord {
    uintptr_t world,pool,descriptor,object;
    uint32_t entity,unit;
    uint8_t bytes[20];
} FpWeaponRecord;
bool fp_read(const FpMemory *,uintptr_t,void *,size_t);
bool fp_pointer(const FpMemory *,uintptr_t,uintptr_t *);
bool fp_u32(const FpMemory *,uintptr_t,uint32_t *);
bool fp_bytes(const FpMemory *,uintptr_t,const char *hex);
uintptr_t fp_unit_object(const FpMemory *,uint32_t ref);
bool fp_weapon_key(const FpMemory *,const FpSnapshot *,uint64_t *);
bool fp_image(const FpMemory *,uintptr_t,uint32_t stamp,uint32_t size);
bool fp_map(const FpMemory *,uintptr_t,uint32_t id,uint32_t bound,uint32_t *index);
bool fp_snapshot(const FpMemory *,bool need_weapon,FpSnapshot *,char *why,size_t why_size);
/* Discover one live weapon independently of the current wield row. This is
 * cleanup evidence, not an equipped-weapon/FPS pose ownership snapshot. */
bool fp_weapon_record(const FpMemory *,uintptr_t world,uint32_t entity,FpWeaponRecord *);
bool fp_same_player(const FpSnapshot *,const FpSnapshot *);
FpProof fp_prove_profile(const FpMemory *);
#endif
