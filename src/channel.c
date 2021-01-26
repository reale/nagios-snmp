/*
 *     N a g i o s   S N M P   T r a p   D a e m o n 
 *
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/


/*
 *     channel.c --- communication with Nagios
 *
 ******************************************************************************
 ******************************************************************************/



#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>

#include "nagiostrapd.h"



/*
 *     Global declarations
 *
 ******************************************************************************/


/* are we a daemon? */
static int channel_is_daemon;

/* handler for Nagios channel */
static FILE *channel_handle;

/*
 * diagnostics counters
 */
static long channel_writes_host = 0;
static long channel_writes_svc = 0;
static size_t channel_written_bytes_host = 0;
static size_t channel_written_bytes_svc = 0;
static int channel_errors = 0;



/*
 *     Private methods
 *
 ******************************************************************************/


/*
 * write a host check result
 */

static void channel_process_host_check_result(unsigned int timestamp, const char *host_name, int return_code, const char *plugin_output, const char *perfdata)
{
	char *buffer;
	int written;

	DEBUG("called");

	if (is_empty(perfdata)) {
		asprintf(&buffer, "[%u] PROCESS_HOST_CHECK_RESULT;%s;%d;%s", timestamp, host_name, return_code, plugin_output);
	} else {
		asprintf(&buffer, "[%u] PROCESS_HOST_CHECK_RESULT;%s;%d;%s | %s", timestamp, host_name, return_code, plugin_output, perfdata);
	}

	DEBUG("message to be written is: %s", buffer);

	if ((written = fprintf(channel_handle, "%s\n", buffer)) < 0) {
		log_error(errno, "cannot write to channel");
		channel_errors++;
	} else {
		channel_writes_host++;
		channel_written_bytes_host += written;
	}
	
	fflush(channel_handle);

	DEBUG("done (written %d bytes)", written);

	free(buffer);
}


/*
 * write a svc check result
 */

static void channel_process_svc_check_result(unsigned int timestamp, const char *host_name, const char *svc_desc, int return_code, const char *plugin_output, const char *perfdata)
{
	char *buffer;
	int written;

	DEBUG("called");

	if (is_empty(perfdata)) {
		asprintf(&buffer, "[%u] PROCESS_SERVICE_CHECK_RESULT;%s;%s;%d;%s", timestamp, host_name, svc_desc, return_code, plugin_output);
	} else {
		asprintf(&buffer, "[%u] PROCESS_SERVICE_CHECK_RESULT;%s;%s;%d;%s | %s", timestamp, host_name, svc_desc, return_code, plugin_output, perfdata);
	}

	DEBUG("message to be written is: %s", buffer);

	if ((written = fprintf(channel_handle, "%s\n", buffer)) < 0) {
		log_error(errno, "cannot write to channel");
		channel_errors++;
	} else {
		channel_writes_svc++;
		channel_written_bytes_svc += written;
	}
	
	fflush(channel_handle);

	DEBUG("done (written %d bytes)", written);

	free(buffer);
}



/*
 *     Public methods
 *
 ******************************************************************************/


/*
 * write to Nagios channel
 */

void channel_write(unsigned int timestamp, const char *host_name, const char *svc_desc, int return_code, const char *plugin_output, const char *perfdata, int is_service)
{
	DEBUG("called; timestamp %u, return_code %d, is_service %s", timestamp, return_code, bool_p(is_service));

	if (is_empty(host_name)) {
		DEBUG("called with empty host_name");
		return;
	} else {
		DEBUG("host_name: %s", host_name);
	}

	if (is_service) {
		if (is_empty(svc_desc)) {
			DEBUG("is_service is TRUE but svc_desc is empty");
			return;
		} else {
			DEBUG("svc_desc: %s", svc_desc);
		}
	}
	
#ifndef NDEBUG
	if (is_empty(plugin_output))
		DEBUG("plugin_output is empty");
	else
		DEBUG("plugin_output: %s", plugin_output);
#endif

	if (channel_is_daemon) {
		get_file_lock(fileno(channel_handle));
		flockfile(channel_handle);
	}

	if (is_service) {
		channel_process_svc_check_result(timestamp, host_name, svc_desc, return_code, plugin_output, perfdata);
	} else {
		channel_process_host_check_result(timestamp, host_name, return_code, plugin_output, perfdata);
	}

	if (channel_is_daemon) {
		funlockfile(channel_handle);
		release_file_lock(fileno(channel_handle));
	}

	DEBUG("done");
}



/*
 *     Class constructor
 *
 ******************************************************************************/


void channel_init(int is_daemon)
{
	const char *channel_path = config_get_option_value(":nagios_pipe");

	DEBUG("called");

	if (is_empty(channel_path))
		log_critical(0, "empty channel path");

	DEBUG("channel path is %s", channel_path);

	if (is_daemon) {
		if ((channel_handle = fopen(channel_path, "a")) == NULL)
			log_critical(errno, "cannot open channel: %s", channel_path);

		channel_is_daemon = 1;
	} else {
		channel_handle = stdout;
		channel_is_daemon = 0;
	}

	DEBUG("done");
}



/*
 *     Callbacks for diagnostics
 *
 ******************************************************************************/


long channel_get_writes_host(void)
{
	return channel_writes_host;
}

long channel_get_writes_svc(void)
{
	return channel_writes_svc;
}

size_t channel_get_written_bytes_host(void)
{
	return channel_written_bytes_host;
}

size_t channel_get_written_bytes_svc(void)
{
	return channel_written_bytes_svc;
}

int channel_get_errors(void)
{
	return channel_errors;
}
