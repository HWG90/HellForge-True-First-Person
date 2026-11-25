/* fp_cha_avatar's bind pose, the left hand and its fingers (rig nodes 13 to 28), and the standard vertical grip's handle,
 * for the grip's finger tests (round 36b). Data, not a game read at run time.
 *
 * The rig: filediver's dump of content/fac_helldivers/fp_cha_avatar/fp_cha_avatar (2026-11-25, the round-36 measurement),
 * each node's bind world matrix as the engine stores it (row vectors: rows 0 to 2 the X, Y, Z axes, row 3 the origin),
 * parent and 32-bit name id; nodes 13 to 28 copied as dumped.
 * The grip: standard_verticalgrip on battle_rifle (the BR-14 Adjudicator's vertical grip), in the gun's frame: its
 * left-hand grip node (IK_left, name 67C94E4B, the node the reach puts the wrist on) and the box of the handle's mesh,
 * from the same dump (rounded to the millimetre as measured). The handle is 4.6 cm across (X) and runs along the gun's
 * Z, the node's -Y: the cylinder the tests wrap the fingers round. */
#ifndef TFP_TEST_FP_HAND_BIND_H
#define TFP_TEST_FP_HAND_BIND_H
#include <stdint.h>

enum { FP_HAND_FIRST = 13, FP_HAND_LAST = 28 };
static const unsigned fp_hand_parent[FP_HAND_LAST + 1 - FP_HAND_FIRST] = {12, 13, 14, 15, 13, 17, 18, 13, 20, 21, 13, 23, 24, 13, 26, 27};
static const uint32_t fp_hand_name[FP_HAND_LAST + 1 - FP_HAND_FIRST] = {
    UINT32_C(0x5c0c68d3), /* 13 l_hand */
    UINT32_C(0x7a19e054), /* 14 l_pinky_finger1 */
    UINT32_C(0xd5371e86), /* 15 l_pinky_finger2 */
    UINT32_C(0x30545cb9), /* 16 l_pinky_finger3 */
    UINT32_C(0x7881f640), /* 17 l_ring_finger1 */
    UINT32_C(0x2db8f964), /* 18 l_ring_finger2 */
    UINT32_C(0xa2917eea), /* 19 l_ring_finger3 */
    UINT32_C(0xe4767875), /* 20 l_middle_finger1 */
    UINT32_C(0xda6cc994), /* 21 l_middle_finger2 */
    UINT32_C(0x83e16077), /* 22 l_middle_finger3 */
    UINT32_C(0xc7d1ff28), /* 23 l_index_finger1 */
    UINT32_C(0xb15bab98), /* 24 l_index_finger2 */
    UINT32_C(0x6cd7940d), /* 25 l_index_finger3 */
    UINT32_C(0xb7d89ff0), /* 26 l_thumb_finger1 */
    UINT32_C(0xc2235d63), /* 27 l_thumb_finger2 */
    UINT32_C(0xdf1f5d92), /* 28 l_thumb_finger3 */
};
static const float fp_hand_world[FP_HAND_LAST + 1 - FP_HAND_FIRST][16] = {
    {-0.64129955, 0.67525846, -0.36436373, 0, -0.5694044, -0.73712635, -0.36389998, 0, -0.51430863, -0.025898546, 0.8572141, 0, -0.6565743, 0.14013085, -0.26326442, 1}, /* 13 */
    {-0.4271531, 0.4332948, -0.7935968, 0, -0.10959989, -0.8960389, -0.430235, 0, -0.8975121, -0.09679804, 0.43023497, 0, -0.71794444, 0.17213896, -0.32840043, 1}, /* 14 */
    {-0.34182888, 0.40751323, -0.8468093, 0, -0.15693547, -0.913189, -0.3761078, 0, -0.92656577, 0.0043299496, 0.37610775, 0, -0.73044467, 0.18481894, -0.3516243, 1}, /* 15 */
    {-0.019118696, 0.38356435, -0.92331624, 0, -0.15693547, -0.913189, -0.3761078, 0, -0.9874237, 0.1377104, 0.077653915, 0, -0.7392496, 0.1953158, -0.3734367, 1}, /* 16 */
    {-0.41352326, 0.60266936, -0.6824868, 0, -0.4162801, -0.7917916, -0.44696397, 0, -0.8097588, 0.09927572, 0.5783037, 0, -0.7110083, 0.193925, -0.32028902, 1}, /* 17 */
    {-0.060581326, 0.44304466, -0.89445037, 0, -0.39516845, -0.83352655, -0.38610265, 0, -0.9166088, 0.33006802, 0.2255736, 0, -0.7258526, 0.21507679, -0.34419042, 1}, /* 18 */
    {0.10743342, 0.40094602, -0.9097805, 0, -0.37535813, -0.8309961, -0.41055027, 0, -0.92063254, 0.38560042, 0.061221763, 0, -0.7295159, 0.2307435, -0.37255216, 1}, /* 19 */
    {-0.32268757, 0.6784428, -0.65999115, 0, -0.48885342, -0.7165513, -0.4975705, 0, -0.8104906, 0.16207913, 0.5628814, 0, -0.7045173, 0.21139184, -0.30844182, 1}, /* 20 */
    {0.007872045, 0.5458787, -0.8378273, 0, -0.5294976, -0.7084763, -0.4665763, 0, -0.8482749, 0.4473005, 0.2834644, 0, -0.71789646, 0.23952115, -0.3358061, 1}, /* 21 */
    {0.007872045, 0.5458787, -0.8378273, 0, -0.6034396, -0.66550624, -0.43927428, 0, -0.79736984, 0.5090362, 0.32416603, 0, -0.7176038, 0.25981474, -0.3669532, 1}, /* 22 */
    {-0.26019984, 0.8317337, -0.49042338, 0, -0.77996475, -0.48045933, -0.40101573, 0, -0.56916684, 0.27816877, 0.7737386, 0, -0.6886408, 0.22918238, -0.30384976, 1}, /* 23 */
    {-0.02998598, 0.6708515, -0.7409853, 0, -0.80320966, -0.4574113, -0.38161358, 0, -0.5949411, 0.5837234, 0.55255055, 0, -0.69788706, 0.25873825, -0.32127708, 1}, /* 24 */
    {-0.02998598, 0.6708515, -0.7409853, 0, -0.78545415, -0.47429857, -0.397621, 0, -0.6181929, 0.57008684, 0.54114556, 0, -0.69884783, 0.28023258, -0.3450185, 1}, /* 25 */
    {0.3239155, 0.9401136, -0.10613714, 0, -0.8726083, 0.3402202, 0.3504354, 0, 0.36555898, -0.020895336, 0.9305536, 0, -0.6434662, 0.16206273, -0.27798167, 1}, /* 26 */
    {0.116583645, 0.83901644, -0.53146935, 0, -0.8832788, 0.33226645, 0.33078322, 0, 0.45412183, 0.43087167, 0.7798223, 0, -0.6287913, 0.20465428, -0.28279018, 1}, /* 27 */
    {0.116583645, 0.83901644, -0.53146935, 0, -0.8749771, 0.33995417, 0.34474066, 0, 0.46991813, 0.42483237, 0.77375335, 0, -0.62459064, 0.23488484, -0.30193955, 1}, /* 28 */
};
/* standard_verticalgrip in the gun's frame: IK_left's origin and axes (X, Y, Z), the handle's box */
static const double vgrip_node_origin[3] = {-0.078, 0.339, -0.007};
static const double vgrip_node_axes[3][3] = {{0.492, 0.871, 0.009}, {-0.078, 0.054, -0.996}, {-0.867, 0.489, 0.094}};
static const double vgrip_handle_min[3] = {-0.023, 0.354, -0.069}, vgrip_handle_max[3] = {0.023, 0.399, 0.061};
#endif
