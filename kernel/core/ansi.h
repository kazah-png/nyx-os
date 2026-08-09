#ifndef ANSI_H
#define ANSI_H
// ANSI/CSI escape-sequence helpers. The terminal parses escape sequences straight out of
// a program's (untrusted) output; a numeric CSI parameter like the N in ESC[Nm is
// accumulated digit by digit, and left unbounded that is signed-integer overflow (UB) the
// moment a hostile stream sends enough digits (e.g. `cat` of a crafted file). See ansi.c.
#include "kernel.h"

// Any real CSI parameter (row, column, SGR code) is tiny; clamp accumulation here — far
// above any legitimate value, so no real sequence is affected.
#define CSI_PARAM_MAX 65535

// Fold one decimal digit (0..9) into a CSI numeric parameter, saturating at CSI_PARAM_MAX
// so no digit run can overflow the signed int or drive it negative. Returns the new value.
int csi_param_accum(int cur, int digit);

// Known-answer test over csi_param_accum (normal accumulation + saturation); 0 if all pass.
int ansi_csi_selftest(void);

#endif // ANSI_H
