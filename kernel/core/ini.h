#ifndef INI_H
#define INI_H

#include "kernel.h"   // fixed-width types

// A hardened, allocation-free reader for INI / .conf configuration text.
//
// ini_get() scans `text` (length `len` bytes — NOT required to be NUL-terminated)
// for the key `key` under section `section`, copying its trimmed value into `out`
// (always NUL-terminated, never writing past `cap`). It is safe on untrusted input:
// every read is bounded by `len`, and a malformed line is skipped rather than trusted.
//
// Grammar (the common INI subset):
//   * lines are split on '\n'; a trailing '\r' is treated as trailing whitespace
//   * a blank line, or one whose first non-blank char is ';' or '#', is a comment
//   * "[section]" opens a section (the name is trimmed inside the brackets)
//   * "key = value" assigns a value; '=' is the only delimiter; the key and value
//     are each trimmed of surrounding blanks (interior blanks in the value are kept)
//   * keys before the first "[section]" live in the global section (section == NULL
//     or "" selects it); section and key matching are case-sensitive and exact
//   * an inline ';'/'#' after a value is NOT a comment — it stays part of the value
//     (matching Python configparser's default), and quotes are never stripped
//   * on a duplicate key the FIRST assignment wins
//
// Returns 1 if the key was found (even with an empty value), 0 otherwise.
int ini_get(const char* text, int len, const char* section, const char* key,
            char* out, int cap);

// Known-answer self-test: 0 on success, else the 1-based index of the first failing
// case (registered as the `iniparse` CI KAT).
int ini_selftest(void);

#endif // INI_H
