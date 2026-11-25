/* Round 38: the "Seat:" line (src/seat_log.h). */
#include "seat_log.h"
#include <string.h>
static int checks, failed;
#define CHECK(c) do { ++checks; if (!(c)) { ++failed; printf("FAILED line %d: %s\n", __LINE__, #c); } } while (0)
int main(void) {
  char line[700];
  TfpSeatLine l = {120, 0, 0, 1, 0, 3, {.03f, .24f, .88f}, {-.2f, .17f, -.49f}, {.2f, .17f, -.49f}, true, 0x40a1b2, 2, 1, 31, 5, 4, 0, 120};
  size_t n = tfp_seat_line(line, sizeof line, &l);
  CHECK(n > 0 && n < sizeof line && !strncmp(line, "Seat: +120 frames from the seat", 31));
  CHECK(strstr(line, "eye (0.03 0.24 0.88) m from the seat's root, anchor settled, glide none"));
  CHECK(strstr(line, "hands from the eye L (-0.20 0.17 -0.49) R (0.20 0.17 -0.49)"));
  CHECK(strstr(line, "diver: 31 rows hidden (5 of the limb slots)"));
  CHECK(strstr(line, "cockpit: seen through (suit 0x40a1b2 by seat, 4 meshes)"));
  CHECK(strstr(line, "arms: rig not drawn by the game here, +0 frames driven, +120 in another camera"));
  l.hands = 1; l.gliding = 1; l.settled = 0; l.see_through = false;
  tfp_seat_line(line, sizeof line, &l);
  CHECK(strstr(line, "hands from the eye not read") && strstr(line, "anchor settling, glide in") && strstr(line, "cockpit: drawn (the option is off)"));
  l.see_through = true; l.suit = 0; l.searches = 3;
  tfp_seat_line(line, sizeof line, &l);
  CHECK(strstr(line, "cockpit: drawn (no suit proven yet, 3 searches)"));
  l.suit = 0x400010; l.cockpit = 0; l.how = 1;
  tfp_seat_line(line, sizeof line, &l);
  CHECK(strstr(line, "cockpit: drawn (suit 0x400010 found, its meshes not taken yet)"));
  CHECK(tfp_seat_line(line, 10, &l) > 10 && strlen(line) == 9);   /* cut, never overrun */
  printf("seat log: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
