#include "libc.h"

/* true — do nothing, successfully. Exit status 0. The companion to `false`, used
 * in shell conditionals and (once sh grows `while`) loop guards: `while true`. */
int main(void) {
    return 0;
}
