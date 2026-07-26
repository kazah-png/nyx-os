#ifndef AUTH_H
#define AUTH_H

#define AUTH_MAX_USER 32
#define AUTH_MAX_PASS 64
#define AUTH_PATH     "/etc/passwd"
#define AVATAR_COUNT  4        /* number of selectable profile pictures (0..3) */

int  auth_setup(void);
int  auth_verify(const char* username, const char* password);
void auth_add_user(const char* username, const char* password, int avatar);
void auth_list_users(void);
int  auth_get_avatar(const char* username);
void auth_set_avatar(const char* username, int avatar);

#endif
