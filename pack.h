#ifndef _PACK_H
#define _PACK_H

#include <stdarg.h>
#include <netinet/in.h>

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
 * 
 */
void unpack(const void *buffer, const char *format, ...);

#endif
