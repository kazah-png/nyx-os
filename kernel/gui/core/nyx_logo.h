#ifndef NYX_LOGO_H
#define NYX_LOGO_H

/* The NyxOS crescent-moon logo as an ASCII shade ramp (" .:o#"), 27 columns x
 * 14 rows. Single source of truth shared by `nyxfetch` (kernel.c) and the login
 * screen (login.c) so the brand mark is identical everywhere. Pure ASCII on
 * purpose: the terminal write path drops bytes >= 0x80, so block glyphs vanish. */

#define NYX_LOGO_ROWS 14
#define NYX_LOGO_COLS 27

static const char* NYX_LOGO[NYX_LOGO_ROWS] = {
    "       .:::o:o#:.          ",
    "    .:oo.. :o.             ",
    "  :oo:.oo.o:               ",
    " .#o:.   :.                ",
    " #:::....:                 ",
    "o#::. . o.                 ",
    "o#.o:   :o                 ",
    "o###o   o#                 ",
    ":#oo::  .oo.               ",
    " o#o:o..  :o:.             ",
    "  o#ooo::.:::#::        .:.",
    "  .:o#oo::.: ..:oo::.o:#o. ",
    "     :o#####:#::o:.::o:    ",
    "        .::oo####::.       ",
};

#endif
