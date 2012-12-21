#ifndef _LOG_H
#define _LOG_H

#include "common.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#define LOG_DEBUG			1
#define LOG_USERINPUT		2
#define LOG_ERROR			4
#define LOG_ERROR_VERBOSE	8
#define LOG_WARNING			16
#define LOG_INFO			32
#define LOG_CONSOLE			64
/* #define LOG_ 				128 */
#define LOG_ALL				255

typedef unsigned char loglevel_t;

struct log_file {
	FILE *file;
	loglevel_t maxlevel;
	bool wrap;
	struct log_file *next;
};

extern struct log_file *log_files;

struct log_file *open_log(const char *filename, loglevel_t maxlevel);
struct log_file *new_log(FILE *file, loglevel_t maxlevel, bool wrap);
struct log_file *close_log(struct log_file*);
void close_logs(void);
int log_message(loglevel_t level, const char *message);
int flog_message(loglevel_t level, const char *format, ...);
int log_error(const char *message);

#endif
