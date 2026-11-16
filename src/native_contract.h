/* Exact build25480438 native-policy witnesses; reviewed target captures. */
#ifndef TFP_NATIVE_CONTRACT_H
#define TFP_NATIVE_CONTRACT_H
#include "backend.h"
typedef struct FpNativeSite {const char *name;bool exe;uint32_t rva;const char *hex;} FpNativeSite;
static const FpNativeSite fp_native_sites[]={
{"unit_position",true,UINT32_C(0x1fc720),"48895c24084889742410574883ec20498bf08bfa8bd9e88511eaff4c8b5018b801000000f0410fc182380017008bc04c"},
{"unit_branch_rotation",true,UINT32_C(0x1fc7a0),"48895c24084889742410574881eca0000000440f29b42490000000498bd84863fae8fa10eaff488bc8488bf04c8b0841"},
{"unit_local_position",true,UINT32_C(0x1fc450),"40534883ec204863dae86214eaff488bc84c8b0041ff90e8000000488bcb48c1e1064883c124488b40204803c14883c4"},
{"camera_position",true,UINT32_C(0x30deb0),"48895c2408574883ec60488bd9498bf88bcae8f9f9d8ff4c8b4b184c8bd04c6343208b4708498bd048c1e206498b4920"},
{"camera_fov",true,UINT32_C(0x30d990),"f30f114934c7415000000000e90f211c00cccccccccccccc"},
{"camera_fov_getter",true,UINT32_C(0x30d980),"f30f104134c3"},
{"graph_getter",true,UINT32_C(0x2bd870),"488d4160c3"},
{"anim_getter",true,UINT32_C(0x2bd9c0),"488b8178010000c3"},
{"camera_fov_call",false,UINT32_C(0x127a11a),"ff9280000000"},
{"camera_position_call",false,UINT32_C(0x127a135),"41ff5120"},
{"rig_position_call",false,UINT32_C(0x1288420),"41ff5138"},
{"rig_rotation_call",false,UINT32_C(0x12890db),"41ff5140"},
{"rig_observe_call",false,UINT32_C(0x1287a41),"41ffd04180bf"},
{"look_stride",false,UINT32_C(0x128990a),"48694dd8ec7a0a00"},
{"look_field",false,UINT32_C(0x1289930),"f20f108401247c0a008b84012c7c0a"},
{"look_clock",false,UINT32_C(0x1289c3e),"488b0503c70902"},
{"look_gain_y",false,UINT32_C(0x12774e6),"f30f594808f30f10842428010000f30f59"},
{"look_gain_x",false,UINT32_C(0x1277511),"f30f103f0f573dd44a1501f30f597804f30f59"},
{"look_gain_table",false,UINT32_C(0x127c056),"4c8d0d33d90702"},
{"camera_scratch_pair",false,UINT32_C(0x127a127),"4c8d45c08b5308488b0b4c8b482041ff5120488b05c8c10a024c8d45008b5308488b0b4c8b482041ff5128488b"},
{"rig_carrier_call",false,UINT32_C(0x12887c9),"41ff5138"},
{"unit_world_matrix",true,UINT32_C(0x1fd220),"40534883ec204863dae89206eaff488bc84c8b0041ff90e8000000488bcb48c1e10648034828488bc14883c4205b"},
{"camera_near_getter",true,UINT32_C(0x30d940),"f30f104128c3"},
{"camera_near_setter",true,UINT32_C(0x30d960),"f30f114928e9a6201c00"},
{"camera_near_setup",false,UINT32_C(0x127bf2c),"488b05d5a30a02f30f100d99a71401498b0f440f29bc2490010000488b5020ff5268488b05b3a30a02f30f100dabbe14"},
};
#define FP_NATIVE_SITE_COUNT (sizeof fp_native_sites/sizeof fp_native_sites[0])
#endif
