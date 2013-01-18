#include "log.h"
#include <time.h>
#include <errno.h>
#include <string.h>

struct log_file *log_files = NULL;

struct log_file *open_log(const char* filename, loglevel_t maxlevel) {
	FILE *file = fopen(filename, "ab");
	if ( file == NULL ) {
		perror("Errore fopen()");
		exit(EXIT_FAILURE);
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
	new->prompt = FALSE;
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

		if ( logfile->prompt ) fputs("\n", logfile->file);

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
			char *pre, *mark, *post = "";
			count++;
			
			/*if ( lf->file == stderr ) {
				pre = "\033[31m";
				post = "\033[0m";
			}*/
			
			if ( lf->prompt && level != LOG_CONSOLE ) pre = "\n";
			else pre = "";
			

			if ( lf->wrap ) switch ( level ) {
				case LOG_DEBUG:
					mark = "((DEBUG))     ";
					break;
				case LOG_USERINPUT:
					mark = "((USERINPUT)) ";
					break;
				case LOG_ERROR:
					mark = "((ERROR))     ";
					break;
				case LOG_ERROR_VERBOSE:
					mark = "((ERROR_VRB)) ";
					break;
				case LOG_WARNING:
					mark = "((WARNING))   ";
					break;
				case LOG_INFO:
					mark = "((INFO))      ";
					break;
				case LOG_INFO_VERBOSE:
					mark = "((INFO_VRB))  ";
					break;
				case LOG_CONSOLE:
					mark = "((CONSOLE))   ";
					break;
				default:
					mark = "((UNDEFINED)) ";
			} else {
				mark = "";
			}

			/*FIXME togliere post se è inutile */
			fprintf(lf->file, "%s%s%s%s\n", pre, mark, message, post);
			if ( lf->prompt ) fprintf(lf->file, "%c ", lf->prompt);
			fflush(lf->file); /*FIXME non flushare inutilmente */
		}
	}

	return count;
}

int log_multiline(loglevel_t level, const char *message) {
	char *split, *start, *end;
	int count;
	
	split = malloc(strlen(message));
	check_alloc(split);
	strcpy(split, message);
	start = end = split;
	
	while ( (end = strchr(start, '\n')) != NULL ) {
		*end = '\0';
		log_message(level, start);
		start = end + 1;
	}
	
	count = log_message(level, start);
	free(split);
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

int log_prompt(struct log_file *logfile) {
	if ( logfile->prompt ) {
		fprintf(logfile->file, "%c ", logfile->prompt);
		fflush(logfile->file);
		return 1;
	}
	return 0;
}
