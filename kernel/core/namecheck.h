#ifndef NAMECHECK_H
#define NAMECHECK_H
// Filename / path-component naming policy. A user-supplied file or directory name must be
// a single, well-formed, control-free UTF-8 component before it is allowed into the
// filesystem — this is the first real caller of the strict UTF-8 decoder (utf8.h). See
// namecheck.c.
#include "kernel.h"

#define NAME_MAX_BYTES 255

// 1 if `name` is a valid single path component: non-empty, <= NAME_MAX_BYTES, well-formed
// UTF-8, no path separator, no C0/C1 control code point or DEL, and not "." or "..".
int name_is_valid(const char* name);

// 1 if the FINAL component (basename) of `path` is valid per name_is_valid (trailing '/'
// ignored). Used to guard user-driven creation (touch/mkdir) while leaving the parent path.
int path_last_component_ok(const char* path);

// Known-answer test over the naming policy; returns 0 if all cases pass.
int namecheck_selftest(void);

#endif // NAMECHECK_H
