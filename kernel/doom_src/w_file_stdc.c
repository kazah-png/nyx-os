//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	WAD I/O functions.
//
 
#include <stdio.h>
#include <string.h>
 
#include "m_misc.h"
#include "w_file.h"
#include "z_zone.h"
 
extern uint8_t* doom_wad_data;
extern uint32_t doom_wad_size;
extern wad_file_class_t direct_wad_file;
 
typedef struct
{
    wad_file_t wad;
    FILE *fstream;
} stdc_wad_file_t;
 
// Direct memory access for DOOM WAD
typedef struct
{
    wad_file_t wad;
    const uint8_t* data;
    uint32_t size;
    uint32_t pos;
} direct_wad_file_t;
 
static wad_file_t* W_Direct_OpenFile(char* path)
{
    extern void serial_puts(const char*);
    extern uint8_t* doom_wad_data;
    extern uint32_t doom_wad_size;
 
    serial_puts("[W_Direct] Checking path: ");
    serial_puts(path);
    serial_puts("\n");
 
    // Check if this is the DOOM WAD
    if (strcasecmp(path, "doom1.wad") == 0 ||
        strcasecmp(path, "/doom1.wad") == 0 ||
        strcasecmp(path, "/boot/doom1.wad") == 0)
    {
        if (doom_wad_data && doom_wad_size > 0)
        {
            serial_puts("[W_Direct] Using direct memory for doom1.wad\n");
            direct_wad_file_t* result = Z_Malloc(sizeof(direct_wad_file_t), PU_STATIC, 0);
            result->wad.file_class = &direct_wad_file;
            result->wad.mapped = NULL;
            result->wad.length = doom_wad_size;
            result->data = doom_wad_data;
            result->size = doom_wad_size;
            result->pos = 0;
            serial_puts("[W_Direct] Direct WAD opened successfully\n");
            return &result->wad;
        }
        else
        {
            serial_puts("[W_Direct] WAD data not available!\n");
        }
    }
 
    return NULL;
}
 
static void W_Direct_CloseFile(wad_file_t* wad)
{
    // Nothing to close for direct memory
    direct_wad_file_t* direct = (direct_wad_file_t*)wad;
    Z_Free(direct);
}
 
size_t W_Direct_Read(wad_file_t* wad, unsigned int offset,
                     void* buffer, size_t buffer_len)
{
    direct_wad_file_t* direct = (direct_wad_file_t*)wad;
 
    if (offset >= direct->size)
        return 0;
 
    uint32_t to_read = buffer_len;
    if (offset + to_read > direct->size)
        to_read = direct->size - offset;
 
    memcpy(buffer, direct->data + offset, to_read);
    return to_read;
}
 
wad_file_class_t direct_wad_file = 
{
    W_Direct_OpenFile,
    W_Direct_CloseFile,
    W_Direct_Read,
};

extern wad_file_class_t stdc_wad_file;

static wad_file_t *W_StdC_OpenFile(char *path)
{
    stdc_wad_file_t *result;
    FILE *fstream;

    fstream = fopen(path, "rb");

    if (fstream == NULL)
    {
        return NULL;
    }

    // Create a new stdc_wad_file_t to hold the file handle.

    result = Z_Malloc(sizeof(stdc_wad_file_t), PU_STATIC, 0);
    result->wad.file_class = &stdc_wad_file;
    result->wad.mapped = NULL;
    result->wad.length = M_FileLength(fstream);
    result->fstream = fstream;

    return &result->wad;
}

static void W_StdC_CloseFile(wad_file_t *wad)
{
    stdc_wad_file_t *stdc_wad;

    stdc_wad = (stdc_wad_file_t *) wad;

    fclose(stdc_wad->fstream);
    Z_Free(stdc_wad);
}

// Read data from the specified position in the file into the 
// provided buffer.  Returns the number of bytes read.

size_t W_StdC_Read(wad_file_t *wad, unsigned int offset,
                   void *buffer, size_t buffer_len)
{
    stdc_wad_file_t *stdc_wad;
    size_t result;

    stdc_wad = (stdc_wad_file_t *) wad;

    // Jump to the specified position in the file.

    fseek(stdc_wad->fstream, offset, SEEK_SET);

    // Read into the buffer.

    result = fread(buffer, 1, buffer_len, stdc_wad->fstream);

    return result;
}


wad_file_class_t stdc_wad_file = 
{
    W_StdC_OpenFile,
    W_StdC_CloseFile,
    W_StdC_Read,
};


