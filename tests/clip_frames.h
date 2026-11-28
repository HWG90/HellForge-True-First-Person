// tests/clip_frames_test.cpp and tests/clip_frames_fov.cpp: the field-of-view helper's side of a frame.
#ifndef TFP_CLIP_FRAMES_H
#define TFP_CLIP_FRAMES_H
void fovw_fresh();
void fovw_aim(bool held);
void fovw_lock(float locked_degrees, float aim);   // aim: TFP_FOV_AIM_ZOOM, TFP_FOV_AIM_LOCKED or a chosen value in radians
float fovw_call(double ms, bool hooked, float native_degrees);
extern "C" { void clip_frames_bind(void); void clip_frames_unbind(void); }
#endif
