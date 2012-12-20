#ifndef _PACK_H
#define _PACK_H

#include <stdarg.h>

/**
 * Format specifiers:
 * b - 8 bit
 * w - 16 bit
 * l - 32 bit
 * s - 0-ended string, strip the \0
 * S - 0-ended string, keep the \0
 * All numbers are to be provided in host-byte-order, they will be converted to network-byte-order.
 */
int pack(void *buffer, const char *format, ...);

/**
 * Format specifiers:
 * b
 * w
 * l
 * s - fixed length string. The length must be provided as argument, before the char* (e.g. unpack(buff, "s", 12, &str) extracts 12 chars).
 * All numbers are extracted from network-byte-order to host-byte-order.
 * Strings will be 0-ended.
 */
void unpack(const void *buffer, const char *format, ...);

#endif
