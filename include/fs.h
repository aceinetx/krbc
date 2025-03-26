#ifndef FS_H
#define FS_H

#include <krbc.h>

byte *fs_read(const char *filename, dword *out_size);

#endif
