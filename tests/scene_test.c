#define main backend_fixture_main
#include "backend_test.c"
#undef main
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
int main(void){
    FpMemory m=setup();FpSnapshot s;char why[160];assert(fp_snapshot(&m,true,&s,why,sizeof why));
    unit(&m,s.body,0xc00000000ULL,107,0xd00000000ULL);n32(0xd00000000ULL+0x2800+90*4,0x8c5570c9);
    n32(0xd00000000ULL+0x2000+90*4,89*65536+1);n32(0xd00000000ULL+0x2800+89*4,0xe9ecca21);
    float head_matrix[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,1.7f,1};put(0xd00000000ULL+0x3000+90*64,head_matrix,64);
    float root[3],head[3],basis[9];assert(fp_body_points(&m,s.body,root,head,basis)&&head[2]==1.7f);
    FpBodySample proof;assert(fp_body_sample(&m,s.body,root,head,basis,&proof)&&fp_body_sample_current(&m,&proof));
    n32(proof.graph.links+proof.head*4,88*65536+1);assert(!fp_body_sample_current(&m,&proof));n32(proof.graph.links+proof.head*4,proof.head_link);
    n32(proof.graph.hashes+89*4,0);assert(!fp_body_sample_current(&m,&proof));n32(proof.graph.hashes+89*4,0xe9ecca21);assert(fp_body_sample_current(&m,&proof));
    FpGraph graph;assert(fp_graph(&m,s.body,&graph)&&graph.count==107);
    n32(graph.hashes+91*4,0x8c5570c9);assert(!fp_body_points(&m,s.body,root,head,basis));n32(graph.hashes+91*4,0);
    unit(&m,s.camera_unit,0xe00000000ULL,1,0xf00000000ULL);p64(0xe00000000ULL+0xa0,0);p64(0xe00000000ULL+0x1d0,0);
    p64(s.camera+0x18,0xe00000000ULL+0x60);n32(s.camera+0x20,0);FpCameraSpace space;
    assert(fp_camera_space(&m,&s,&space)&&space.rotation.w==1&&!space.parent);
    FpCameraSpace same=space;assert(fp_camera_space_same(&space,&same));same.parent=0x12345;assert(!fp_camera_space_same(&space,&same));
    p64(0xe00000000ULL+0x1d0,0xc00000000ULL);n32(0xf00000000ULL+0x2000,89*65536+2);p64(0xe00000000ULL+0x78,0xd00000000ULL+0x3000);
    float parent_matrix[16]={1,0,0,0,0,1,0,0,0,0,1,0,5,6,7,1};put(0xd00000000ULL+0x3000+89*64,parent_matrix,64);
    assert(fp_camera_space(&m,&s,&space)&&space.parent_ref==s.body&&space.position[0]==5);
    p64(0xe00000000ULL+0x78,0xd00000000ULL+0x4000);assert(!fp_camera_space(&m,&s,&space));
    puts("C scene: head identity, duplicate rejection, anonymous camera, parent space and stale attachment checks passed");return 0;
}
