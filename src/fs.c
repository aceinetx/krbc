#include <krbc.h>

byte *fs_read(const char *filename, dword *out_size) {
  FILE *file;
  long fileSize;
  byte *buffer;
  dword bytesRead;

  file = fopen(filename, "rb");
  if (!file) {
    return NULL;
  }

  fseek(file, 0, SEEK_END);
  fileSize = ftell(file);
  fseek(file, 0, SEEK_SET);

  if (fileSize < 0) {
    fclose(file);
    return NULL;
  }

  buffer = (byte *)malloc(fileSize);
  if (!buffer) {
    fclose(file);
    return NULL;
  }

  bytesRead = fread(buffer, 1, fileSize, file);
  if (bytesRead != fileSize) {
    free(buffer);
    fclose(file);
    return NULL;
  }

  fclose(file);

  if (out_size) {
    *out_size = (dword)fileSize;
  }

  return buffer;
}
