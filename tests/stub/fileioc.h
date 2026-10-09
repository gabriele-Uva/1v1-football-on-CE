/* Host-side stand-in for CEdev's fileioc.h (test harness only). */
#ifndef STUB_FILEIOC_H
#define STUB_FILEIOC_H
#include <stdint.h>
#include <stddef.h>

uint8_t ti_Open(const char *name, const char *mode);
int ti_Close(uint8_t handle);
size_t ti_Write(const void *data, size_t size, size_t count, uint8_t handle);
size_t ti_Read(void *data, size_t size, size_t count, uint8_t handle);
int ti_SetArchiveStatus(uint8_t archive, uint8_t handle);
#endif
