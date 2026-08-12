#ifndef FILETYPE_H
#define FILETYPE_H

#include "../core/kernel.h"

// Identify a file's type from its leading bytes (magic numbers) with a name/extension
// refinement for text files — the classic file(1) classifier. `name` may be NULL (then
// only the content is used). Returns a static human-readable type string (never NULL).
// Reuses utf8_validate() to tell ASCII text from UTF-8 text and both from binary "data".
const char* filetype_identify(const char* name, const uint8_t* data, uint32_t len);

// Short human-readable type label for a directory listing, from the NAME/extension
// only (no content read — cheap to call once per entry). is_dir != 0 -> "Folder".
// Used by the File Manager's Type column.
const char* filetype_label(const char* name, int is_dir);

int filetype_selftest(void);

#endif // FILETYPE_H
