/*
 *     N a g i o s   S N M P   T r a p   D a e m o n 
 *
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/


/*
 *     log.c --- logging routines
 *
 ******************************************************************************
 ******************************************************************************/



#define _GNU_SOURCE /* strerror_r() */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <pthread.h>

#ifndef NDEBUG
# include <unistd.h> /* getpid() */
# include <sys/types.h>
#endif

#include "nagiostrapd.h"



/*
 *     Global declarations
 *
 ******************************************************************************/


/* line mode */
typedef enum { LOG_LINE_BEGINS, LOG_LINE_ENDS, LOG_LINE_NONE } log_linemode_t;

/* is this class already initialized? */
static int log_init_completed = 0;

/* are we a daemon? */
static int log_is_daemon;

/* verbosity level of logging */
static log_verbosity_t log_verbosity;

/* error log file */
static FILE *log_error_handler;

#ifndef NDEBUG

/* debug log file */
static FILE *log_debug_handler;

#endif /* NDEBUG */



/*
 *     Private methods
 *
 ******************************************************************************/

/*
 * write a prefix to log file (date, etc.)
 */

#define LOG_PREFIX_BUFFER 1024

static void log_write_prefix(FILE *handler)
{
	struct tm local;
	time_t t;
	char buffer[LOG_PREFIX_BUFFER];

	t = time(NULL);
	localtime_r(&t, &local);
	asctime_r(&local, buffer);

	if (is_empty(buffer))
		return;
	
	/* we don't need the trailing newline */
	buffer[strlen(buffer)-1] = '\0';

	get_file_lock(fileno(handler));
	fprintf(handler, "%s ", buffer);
	release_file_lock(fileno(handler));
}


/*
 * write to log file (va_list mode)
 */

static void log_write_v(FILE *handler, log_linemode_t mode, const char *format, va_list ap)
{
	int append_newline = 0;

	switch (mode) {
		case LOG_LINE_BEGINS:
			log_write_prefix(handler);
			break;
		case LOG_LINE_ENDS:
			append_newline = 1;
			break;
		case LOG_LINE_NONE:
			break;
		default:
			return;
	}

	get_file_lock(fileno(handler));
	vfprintf(handler, format, ap);

	if (append_newline)
		fprintf(handler, "\n");
	release_file_lock(fileno(handler));
}


/*
 * write to log file (vararg mode)
 */

static void log_write(FILE *handler, log_linemode_t mode, const char *format, ...)
{
	va_list ap;
	int append_newline = 0;

	switch (mode) {
		case LOG_LINE_BEGINS:
			log_write_prefix(handler);
			break;
		case LOG_LINE_ENDS:
			append_newline = 1;
			break;
		case LOG_LINE_NONE:
			break;
		default:
			return;
	}

	get_file_lock(fileno(handler));
	va_start(ap, format);
	log_write_v(handler, LOG_LINE_NONE, format, ap);
	va_end(ap);

	if (append_newline)
		fprintf(handler, "\n");
	release_file_lock(fileno(handler));
}


/*
 * write description string associated to error number
 */

#define ERR_BUF_LEN 1024

static void log_strerror(FILE *handler, int errnum)
{
	char *buf, *buf2;
	
	if (errnum != 0) {
		buf = xmalloc(ERR_BUF_LEN);
		buf2 = strerror_r(errnum, buf, ERR_BUF_LEN);
		log_write(handler, LOG_LINE_ENDS, ": %s", buf2);
		free(buf);
	}
}



/*
 *     Public methods
 *
 ******************************************************************************/

#ifndef NDEBUG

/*
 * write debug entry
 */

void log_debug(const char *file, long line, const char *func, const char *format, ...)
{
	va_list ap;

	if (!log_init_completed)
		return;

	if (log_verbosity > LOG_VERBOSITY_DEBUG)
		return;


	flockfile(log_debug_handler);
	log_write(log_debug_handler, LOG_LINE_BEGINS,
		"DEBUG: [pid: %d] [tid: %d]: %s[%ld]: %s(): ", getpid(), gettid(), file, line, func);

	va_start(ap, format);
	log_write_v(log_debug_handler, LOG_LINE_ENDS, format, ap);
	va_end(ap);

	fflush(log_debug_handler);

	funlockfile(log_debug_handler);
} 

#endif /* NDEBUG */


/*
 * write warning entry
 */

void log_warning(int errnum, const char *format, ...)
{
	va_list ap;

	if (!log_init_completed)
		return;

	if (log_verbosity > LOG_VERBOSITY_WARNING)
		return;
	
	DEBUG("WARNING annotation added to error log");


	flockfile(log_error_handler);
	log_write(log_error_handler, LOG_LINE_BEGINS, "WARNING: ");

	va_start(ap, format);
	log_write_v(log_error_handler, LOG_LINE_NONE, format, ap);
	va_end(ap);

	if (errnum != 0)
		log_strerror(log_error_handler, errnum);
	else
		log_write(log_error_handler, LOG_LINE_NONE, "\n");

	fflush(log_error_handler);

	funlockfile(log_error_handler);
} 


/*
 * write error entry
 */

void log_error(int errnum, const char *format, ...)
{
	va_list ap;

	if (!log_init_completed)
		return;

	if (log_verbosity > LOG_VERBOSITY_ERROR)
		return;

	DEBUG("ERROR annotation added to error log");

	flockfile(log_error_handler);

	log_write(log_error_handler, LOG_LINE_BEGINS, "ERROR: ");

	va_start(ap, format);
	log_write_v(log_error_handler, LOG_LINE_NONE, format, ap);
	va_end(ap);

	if (errnum != 0)
		log_strerror(log_error_handler, errnum);
	else
		log_write(log_error_handler, LOG_LINE_NONE, "\n");

	fflush(log_error_handler);

	funlockfile(log_error_handler);
} 


/*
 * write critical entry and exits
 */

void log_critical(int errnum, const char *format, ...)
{
	va_list ap;
	FILE *handler;

	DEBUG("CRITICAL annotation added to error log");

	if (log_init_completed)
		handler = log_error_handler;
	else
		handler = stderr;

	flockfile(handler);
	log_write(handler, LOG_LINE_BEGINS, "CRITICAL: ");

	va_start(ap, format);
	log_write_v(handler, LOG_LINE_NONE, format, ap);
	va_end(ap);

	if (errnum != 0)
		log_strerror(handler, errnum);
	else
		log_write(handler, LOG_LINE_NONE, "\n");

	/* we're exiting, so don't bother to release the lock */

	if (log_is_daemon && !startup_is_completed())
		fprintf(stderr, "startup failed, see error log\n");
	
	monitor_kill_children();

	exit(EXIT_FAILURE);
} 



/*
 *     Class constructor
 *
 ******************************************************************************/


void log_init(int is_daemon, int verbose)
{
	/* `verbose' is only honoured when not in daemon mode */

	if (is_daemon) {
		char *verbosity;

		verbosity = config_get_option_value(":log_verbosity");
		if (verbosity == NULL)
			log_verbosity = LOG_VERBOSITY_WARNING;
		else if (!strcmp(verbosity, "debug"))
			log_verbosity = LOG_VERBOSITY_DEBUG;
		else if (!strcmp(verbosity, "warning"))
			log_verbosity = LOG_VERBOSITY_WARNING;
		else if (!strcmp(verbosity, "error"))
			log_verbosity = LOG_VERBOSITY_ERROR;
		else if (!strcmp(verbosity, "critical"))
			log_verbosity = LOG_VERBOSITY_CRITICAL;
		else
			log_verbosity = LOG_VERBOSITY_WARNING;

		if ((log_error_handler = fopen(config_get_option_value(":error_log"), "a")) == NULL) {
			perror("cannot open error log for writing");
			exit(EXIT_FAILURE);
		}

#ifndef NDEBUG
		if (log_verbosity == LOG_VERBOSITY_DEBUG
			&& (log_debug_handler = fopen(config_get_option_value(":debug_log"), "a")) == NULL) {
			perror("cannot open debug log");
			exit(EXIT_FAILURE);
		}
#endif /* NDEBUG */

	} else {
		log_error_handler = stderr;
#ifndef NDEBUG
		log_debug_handler = stderr;
#endif /* NDEBUG */

		log_verbosity = (verbose ? LOG_VERBOSITY_DEBUG : LOG_VERBOSITY_WARNING);
	}

	log_is_daemon = is_daemon;
	log_init_completed = 1;
}



/*
 *     Callbacks for diagnostics
 *
 ******************************************************************************/

size_t log_get_log_error_size(void)
{
	return get_file_size(log_error_handler);
}

#ifndef NDEBUG
size_t log_get_log_debug_size(void)
{
	return get_file_size(log_debug_handler);
}
#endif /* NDEBUG */
