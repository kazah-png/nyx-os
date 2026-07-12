#include "libc.h"

/* clear — clear the terminal. ESC[3J tells the NyxOS terminal to erase its scrollback
 * and return to the top WITHOUT entering full-screen (TUI) mode, so it composes
 * cleanly when run from inside the userspace shell. */

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    write(1, "\033[3J", 4);
    return 0;
}
