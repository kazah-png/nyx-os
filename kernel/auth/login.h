#ifndef LOGIN_H
#define LOGIN_H

int login_screen(void);

// Profile pictures (4 procedural avatars), shared with the desktop taskbar.
void draw_avatar(int x, int y, int size, int id, int selected);
const char* avatar_name(int id);

// The account that logged in (set by login_screen on success; guest 'nyx' by
// default). The taskbar shows this name + avatar as the "current user".
extern char g_login_user[32];
extern int  g_login_avatar;
extern char g_login_home[64];       // this session's home dir (/home/<user>)
void* login_home_node(void);        // home dir node (or root) for a new Terminal's CWD

#endif
