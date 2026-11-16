#ifndef TFP_PRESENTATION_H
#define TFP_PRESENTATION_H
#include <stdbool.h>
#include <stdint.h>
typedef struct FpQuat {
  float x, y, z, w;
} FpQuat;
FpQuat fp_q_identity(void);
FpQuat fp_q_mul(FpQuat, FpQuat);
FpQuat fp_q_inverse(FpQuat);
bool fp_q_valid(FpQuat);
void fp_q_rotate(FpQuat, const float *, float *);
FpQuat fp_q_look(float yaw, float pitch);
bool fp_q_angles(FpQuat, float *, float *);
bool fp_q_matrix(const float *, FpQuat *);
#endif
