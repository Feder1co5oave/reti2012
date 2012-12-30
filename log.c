#include "log.h"
#include "common.h"
#include <time.h>
#include <errno.h>
#include <string.h>

struct log_file *log_files = NULL;

struct log_file *open_log(const char* filename, loglevel_t maxlevel) {
	FILE *file = fopen(filename, "ab");
	if ( file == NULL ) {
		perror("Errore fopen()");
		exit(1);
	}
	return new_log(file, maxlevel, TRUE); /* wrapped */
}

struct log_file *new_log(FILE *file, loglevel_t maxlevel, bool wrap) {
	time_t now = time(NULL);
	struct log_file *new = malloc(sizeof(struct log_file));
	check_alloc(new);
	new->file = file;
	new->maxlevel = maxlevel;
	new->wrap = wrap;
	new->next = NULL;

	/* We don't want log delimiters on the console */
	if ( wrap && file != stdout && file != stderr )
		fprintf(file, "======== Opening logfile at %s", ctime(&now));
		/* ctime() terminates with \n\0 */

	if ( log_files != NULL ) {
		struct log_file *ptr = log_files;
		while ( ptr->next != NULL ) ptr = ptr->next;
		ptr->next = new;
	} else {
		log_files = new;
		/* close logs on process termination */
		atexit(close_logs);
		/*TODO set signal handler on ^C too */
	}
	return new;
}

struct log_file *close_log(struct log_file *logfile) {
	struct log_file *lf = NULL;
	if ( logfile != NULL ) {
		if ( logfile == log_files ) {
			log_files = logfile->next;
		} else {
			lf = log_files;
			while ( lf != NULL && lf->next != logfile ) lf = lf->next;
			if ( lf != NULL ) lf->next = logfile->next;
		}

		lf = logfile->next;

		if ( logfile->wrap && logfile->file != stdout && logfile->file != stderr ) {
			time_t now = time(NULL);
			fprintf(logfile->file, "======== Closing logfile at %s\n\n",
				ctime(&now));
		}

		fflush(logfile->file);
		/* prevent stdout/stderr from being fclose()d */
		if ( logfile->file != stdout && logfile->file != stderr )
			fclose(logfile->file);

		free(logfile);
	}
	return lf;
}

void close_logs() {
	struct log_file *lf;
	for ( lf = log_files; lf != NULL; lf = close_log(lf) );
}

int log_message(loglevel_t level, const char *message) {
	struct log_file *lf;
	int count = 0;
	for ( lf = log_files; lf != NULL; lf = lf->next ) {
		if ( level & lf->maxlevel ) {
			count++;
			if ( lf->wrap ) switch ( level ) {
				case LOG_DEBUG:
					fprintf(lf->file, "((DEBUG)) %s\n", message);
					break;
				case LOG_USERINPUT:
					fprintf(lf->file, "((USERINPUT)) %s\n", message);
					break;
				case LOG_ERROR:
					fprintf(lf->file, "((ERROR)) %s\n", message);
					break;
				case LOG_ERROR_VERBOSE:
					fprintf(lf->file, "((ERROR_VERBOSE)) %s\n", message);
					break;
				case LOG_WARNING:
					fprintf(lf->file, "((WARNING)) %s\n", message);
					break;
				case LOG_INFO:
					fprintf(lf->file, "((INFO)) %s\n", message);
					break;
				case LOG_INFO_VERBOSE:
					fprintf(lf->file, "((INFO_VERBOSE)) %s\n", message);
					break;
				case LOG_CONSOLE:
					fprintf(lf->file, "((CONSOLE)) %s\n", message);
					break;
				default:
					fprintf(lf->file, "((UNDEFINED)) %s\n", message);
			} else {
				fputs(message, lf->file);
				fputs("\n", lf->file);
				fflush(lf->file); /* è unwrapped, va bene così */
			}
		}
	}

	return count;
}

int flog_message(loglevel_t level, const char* format, ...) {
	va_list args;
	int count;
	char *message = malloc(BUFFER_SIZE);
	check_alloc(message);
	va_start(args, format);
	vsprintf(message, format, args);
	count = log_message(level, message);
	free(message);
	va_end(args);
	return count;
}

int log_error(const char *message) {
	return flog_message(LOG_ERROR, "%s: %s", message, strerror(errno));
}
