/*
 *     N a g i o s   S N M P   T r a p   D a e m o n 
 *
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/


/*
 *     diagnostics.c --- diagnostics support
 *
 ******************************************************************************
 ******************************************************************************/



#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <malloc.h>
#include <errno.h>
#include <pthread.h>
#include <dirent.h>

#include "nagiostrapd.h"



/*
 *     Global declarations
 *
 ******************************************************************************/


/* timestamp we started up at */
static time_t startup_time = 0;

/* handler for diagnostics dump file */
static FILE *diagnostics_handler;



/*
 *     Static methods
 *
 ******************************************************************************/


static double get_rate_per_sec(double abs)
{
	int timediff = time(NULL) - startup_time;
	double rate;
	
	if (timediff)
		rate = abs/timediff;
	else
		rate = 0;

	return rate;
}


static double diagnostics_get_trap_log_size(void)
{
	return (double) trap_get_trap_log_size();
}

static double diagnostics_get_log_error_size(void)
{
	return (double) log_get_log_error_size();
}

#ifndef NDEBUG
static double diagnostics_get_log_debug_size(void)
{
	return (double) log_get_log_debug_size();
}
#endif

static double diagnostics_get_channel_written_bytes_host(void)
{
	return (double) channel_get_written_bytes_host();
}

static double diagnostics_get_channel_written_bytes_svc(void)
{
	return (double) channel_get_written_bytes_svc();
}

static double diagnostics_get_channel_written_bytes_total(void)
{
	return (double) (channel_get_written_bytes_host() + channel_get_written_bytes_svc());
}

static double diagnostics_get_channel_written_bytes_host_per_sec(void)
{
	return get_rate_per_sec(channel_get_written_bytes_host());
}

static double diagnostics_get_channel_written_bytes_svc_per_sec(void)
{
	return get_rate_per_sec(channel_get_written_bytes_svc());
}

static double diagnostics_get_channel_written_bytes_total_per_sec(void)
{
	return get_rate_per_sec(channel_get_written_bytes_host() + channel_get_written_bytes_svc());
}

static double diagnostics_get_channel_writes_host(void)
{
	return (double) channel_get_writes_host();
}

static double diagnostics_get_channel_writes_svc(void)
{
	return (double) channel_get_writes_svc();
}

static double diagnostics_get_channel_writes_total(void)
{
	return (double) (channel_get_writes_host() + channel_get_writes_svc());
}

static double diagnostics_get_channel_writes_host_per_sec(void)
{
	return get_rate_per_sec(channel_get_writes_host());
}

static double diagnostics_get_channel_writes_svc_per_sec(void)
{
	return get_rate_per_sec(channel_get_writes_svc());
}

static double diagnostics_get_channel_writes_total_per_sec(void)
{
	return get_rate_per_sec(channel_get_writes_host() + channel_get_writes_svc());
}

static double diagnostics_get_channel_errors(void)
{
	return (double) channel_get_errors();
}

static double diagnostics_get_num_of_threads(void)
{
	return (double) worker_get_num_of_threads();
}

static double diagnostics_get_length_of_tasks_queue(void)
{
	return (double) worker_get_length_of_tasks_queue();
}

static double diagnostics_get_length_of_free_tasks_queue(void)
{
	return (double) worker_get_length_of_free_tasks_queue();
}

static double diagnostics_get_malloc_arena(void)
{
	struct mallinfo malloc_info = mallinfo();
	return (double) malloc_info.arena;
}

static double diagnostics_get_parsed_traps(void)
{
	return (double) trap_get_trap_parsed();
}

static double diagnostics_get_parsed_traps_per_sec(void)
{
	return get_rate_per_sec(trap_get_trap_parsed());
}

static double diagnostics_get_pid(void)
{
	return (double) getpid();
}

static double diagnostics_get_uptime(void)
{
	time_t now = time(NULL);
	return (double) (now - startup_time);
}

static double diagnostics_get_used_memory(void)
{
	struct mallinfo malloc_info = mallinfo();
	return (double) (malloc_info.usmblks + malloc_info.uordblks);
}



/*
 *     Diagnostics table
 *
 ******************************************************************************/


typedef double (*diagnostics_hook_t)(void);

struct diagnostics_table {
	char *key;
	diagnostics_hook_t hook;
	int cumulative;
	int integer;
};


struct diagnostics_table diagnostics[] = {
	{ "Pid", diagnostics_get_pid, 0, 1 },
	{ "Uptime", diagnostics_get_uptime, 0, 1 },
	{ "Available Threads", diagnostics_get_num_of_threads, 1, 1 },
	{ "Tasks Queue Length", diagnostics_get_length_of_tasks_queue, 1, 1 }, 
	{ "Free Tasks Queue Length", diagnostics_get_length_of_free_tasks_queue, 1, 1 },
	{ "Malloc Arena", diagnostics_get_malloc_arena, 1, 1 },
	{ "Used Memory", diagnostics_get_used_memory, 1, 1 },
	{ "Parsed Traps", diagnostics_get_parsed_traps, 1, 1},
	{ "Parsed Traps/sec", diagnostics_get_parsed_traps_per_sec, 1, 0 },
	{ "Channel Host Written Bytes", diagnostics_get_channel_written_bytes_host, 1, 1 },
	{ "Channel Svc Written Bytes", diagnostics_get_channel_written_bytes_svc, 1, 1 },
	{ "Channel Total Written Bytes", diagnostics_get_channel_written_bytes_total, 1, 1 },
	{ "Channel Host Written Bytes/sec", diagnostics_get_channel_written_bytes_host_per_sec, 1, 0 },
	{ "Channel Svc Written Bytes/sec", diagnostics_get_channel_written_bytes_svc_per_sec, 1, 0 },
	{ "Channel Total Written Bytes/sec", diagnostics_get_channel_written_bytes_total_per_sec, 1, 0 },
	{ "Channel Host Writes", diagnostics_get_channel_writes_host, 1, 1 },
	{ "Channel Svc Writes", diagnostics_get_channel_writes_svc, 1, 1 },
	{ "Channel Total Writes", diagnostics_get_channel_writes_total, 1, 1 },
	{ "Channel Host Writes/sec", diagnostics_get_channel_writes_host_per_sec, 1, 0 },
	{ "Channel Svc Writes/sec", diagnostics_get_channel_writes_svc_per_sec, 1, 0 },
	{ "Channel Total Writes/sec", diagnostics_get_channel_writes_total_per_sec, 1, 0 },
	{ "Channel Errors", diagnostics_get_channel_errors, 1, 1 },
	{ "Error Log Size", diagnostics_get_log_error_size, 0, 1 },
#ifndef NDEBUG
	{ "Debug Log Size", diagnostics_get_log_debug_size, 0, 1 },
#endif
	{ "Trap Log Size", diagnostics_get_trap_log_size, 0, 1 },
	{ NULL, NULL, 0, 0 }
};



/*
 *     Public methods
 *
 ******************************************************************************/


size_t diagnostics_get_padding(void)
{
	size_t padding = 0;
	int i;

	for (i = 0 ;; i++) {
		size_t keylen;
		if (diagnostics[i].key == NULL)
			break;
		if (padding < (keylen = strlen(diagnostics[i].key)))
			padding = keylen;
	}

	padding += 8;

	return padding;
}


char *diagnostics_get_key(int index)
{
	return diagnostics[index].key;
}


int diagnostics_get_cumulative(int index)
{
	return diagnostics[index].cumulative;
}


int diagnostics_get_integer(int index)
{
	return diagnostics[index].integer;
}


char *diagnostics_prepare_write(void)
{
	char *buffer, *aux;
	size_t buflen = TMPBUFLEN, auxlen, usedlen = 0;
	static size_t padding = 0;
	int i;

	if (!padding)
		padding = diagnostics_get_padding();

	buffer = xmalloc(buflen);

	for (i = 0 ;; i++) {
		if (diagnostics[i].key == NULL)
			break;

		if (diagnostics[i].hook != NULL) {
			double value = diagnostics[i].hook();

			if (diagnostics[i].integer)
				auxlen = asprintf(&aux, "%ld", (long) value);
			else
				auxlen = asprintf(&aux, "%0.3f", (float) value);

			if (usedlen + padding + auxlen + 1 < buflen) {
				buflen += TMPBUFLEN + padding + auxlen + 1;
				buffer = xrealloc(buffer, buflen);
			}

			memset(buffer+usedlen, 0x20, padding);
			memcpy(buffer+usedlen, diagnostics[i].key, strlen(diagnostics[i].key));
			memcpy(buffer+usedlen+padding, aux, auxlen);
			free(aux);
			usedlen += padding + auxlen + 1;
			buffer[usedlen-1] = '\n';
		}
	}

	buffer[usedlen-1] = '\n';
	buffer[usedlen] = '\0';

	return buffer;
}


/*
 * write diagnostics file
 */

void diagnostics_write(void)
{
	char *diagnostics_file;
	char *buffer;

	asprintf(&diagnostics_file, "%s/%d", config_get_option_value(":diagnostics_pool"), getpid());

	if ((diagnostics_handler = fopen(diagnostics_file, "w")) == NULL) {
		flockfile(diagnostics_handler);
		log_error(errno, "cannot open diagnostics file %s", diagnostics_file);
		return;
	}

	buffer = diagnostics_prepare_write();

	if (buffer == NULL) {
		log_error(0, "diagnostics buffer returned NULL");
		return;
	}

	fputs(buffer, diagnostics_handler);
	funlockfile(diagnostics_handler);
	fclose(diagnostics_handler);

	free(buffer);
}


void diagnostics_pool_clear(void)
{
	DIR *pool;
	struct dirent *dir_entry;

	if ((pool = opendir(config_get_option_value(":diagnostics_pool"))) == NULL)
		log_error(errno, "cannot read diagnostics pool");
	
	while ((dir_entry = readdir(pool)) != NULL) {
		if (dir_entry->d_type == DT_REG && unlink(dir_entry->d_name) < 0)
			log_error(errno, "cannot remove diagnostics file %s", dir_entry->d_name);
	}
}


/*
 *     Class constructor
 *
 ******************************************************************************/


void diagnostics_init(void)
{
	startup_time = time(NULL);
}

