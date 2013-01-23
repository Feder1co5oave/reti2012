#ifndef _LOG_H
#define _LOG_H

#include "common.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>



/* ===[ Data types ]========================================================= */

/**
 * Allows for up to 8 different log levels
 */
typedef unsigned char loglevel_t;

/**
 * A log file. (!)
 */
struct log_file {
	FILE *file;
	loglevel_t maxlevel;
	bool wrap;
	char prompt;
	struct log_file *next;
};



/* ===[ Log levels ]========================================================= */

#define LOG_DEBUG             1
#define LOG_USERINPUT         2
#define LOG_ERROR             4
#define LOG_ERROR_VERBOSE     8
#define LOG_WARNING          16
#define LOG_INFO             32
#define LOG_INFO_VERBOSE     64
#define LOG_CONSOLE         128
#define LOG_ALL             255



/* ===[ Data ]=============================================================== */

/**
 * The list of opened log_files.
 */
extern struct log_file *log_files;



/* ===[ Functions ]========================================================== */

/**
 * Create a new log_file using a new or an existing file on the filesystem.
 * The file is opened in APPEND mode.
 * Log messages are written on the log_file if their (level & maxlevel) != 0.
 * All log_files opened with this function are wrapped.
 * @param const char *filename the filename of a new or existing file on the
 * filesystem
 * @param loglevel_t maxlevel a combination of log levels obtained by or-ing
 * multiple log level constants
 * @return the newly created log_file
 */
struct log_file *open_log(const char *filename, loglevel_t maxlevel);

/**
 * Create a new log_file using an already open FILE descriptor (writable). This
 * function can be used to log messages to special FILEs, e.g. stdout, stderr.
 * Log messages are written on the log_file if their (level & maxlevel) != 0.
 * Wrapped log_files will have log delimiters with opening and closing times and
 * the log level associated to every single log message.
 * @param FILE *file a writable FILE descriptor
 * @param loglevel_t maxlevel a combination of log levels obtained by or-ing
 * multiple log level constants
 * @param bool wrap whether the log_file should be wrapped or unwrapped
 * @return the newly created log_file
 */
struct log_file *new_log(FILE *file, loglevel_t maxlevel, bool wrap);

/**
 * Close a log_file.
 * Before closing, write a closing delimiter if the log_file is wrapped.
 * @param struct log_file *logfile
 * @return the ->next member of the now closed log_file
 */
struct log_file *close_log(struct log_file*);

/**
 * Close all open log_files.
 * @see close_log(struct log_file*)
 */
void close_logs(void);

/**
 * Log a message to all open log_files with (level & maxlevel) != 0.
 * @param loglevel_t level the log level of the message, normally one of the
 * LOG_* constants, except LOG_ALL. OR-ing multiple LOG_* will cause the message
 * to be printed on all log_files associated with at least one of the OR-ed
 * levels, but to show marked as '((UNDEFINED))' on wrapped log_files.
 * @param const char *message the string to be logged, preferably without any
 * trailing space
 * @return int the number of open log_files with matching level
 */
int log_message(loglevel_t level, const char *message);

/**
 * Print a multiline message. Each line will have its own level mark.
 * @param loglevel_t level @see log_message()
 * @param const char *message the multiline message, lines are separated by \n
 * @return int @see log_message()
 */
int log_multiline(loglevel_t level, const char *message);

/**
 * Log a formatted message to all open log_files with (level & maxlevel) != 0.
 * @see printf(const char *format, ...)
 * @param loglevel_t level @see log_message()
 * @param const char *format the format string containing some format specifiers
 * @param ... additional arguments, used to replace specifiers in the format
 * string
 * @return int @see log_message()
 */
int flog_message(loglevel_t level, const char *format, ...);

/**
 * Log a message of level LOG_ERROR followed by a description of the error
 * specified in errno.
 * @see strerror()
 * @param const char *message
 * @return int @see log_message()
 */
int log_error(const char *message);

/**
 * Print a prompt on a log_file, if logfile->prompt is set.
 * @param struct log_file *logfile
 * @return int 1 if logfile->prompt is set, 0 otherwise
 */
int log_prompt(struct log_file *file);

/* ========================================================================== */

#endif
