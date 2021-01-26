/*
 *     N a g i o s   S N M P   T r a p   D a e m o n 
 *
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/


/*
 *     traplog.c --- trap logging
 *
 ******************************************************************************
 ******************************************************************************/


#include <stdlib.h>
#include <time.h>
#include <errno.h>

#include "nagiostrapd.h"



/*
 *     Global declarations
 *
 ******************************************************************************/



/* delimiter string */
static char *trap_log_delimiter;

/* trap log handler */
static FILE *trap_log_handler;

/* trap rate threshold used to switch between block-oriented IO */
static float trap_rate_threshold = 0;

/* timestamp we started up at */
static time_t startup_time = 0;



/*
 *     Private methods
 *
 ******************************************************************************/


static float get_trap_rate(void)
{
	int timediff = time(NULL) - startup_time;
	long traps = trap_get_trap_parsed();
	float rate;
	
	if (timediff)
		rate = traps/timediff;
	else
		rate = 0;

	return rate;
}


static void flush_log(void)
{
	if (get_trap_rate() < trap_rate_threshold)
		fflush(trap_log_handler);
}



/*
 *     Public methods
 *
 ******************************************************************************/


void traplog_log(struct trap_t *trap, int executed)
{
	char *buffer = trap_serialize(trap, executed, trap_log_delimiter);

	get_file_lock(fileno(trap_log_handler));
	flockfile(trap_log_handler);
	fprintf(trap_log_handler, "%s\n", buffer);
	flush_log();
	funlockfile(trap_log_handler);
	release_file_lock(fileno(trap_log_handler));

	free(buffer);
}



/*
 *     Class constructor
 *
 ******************************************************************************/

void traplog_init(void)
{
	float bogomips = 0;

	DEBUG("called");

	trap_log_delimiter = config_get_option_value(":trap_log_delimiter");
	if (is_empty(trap_log_delimiter)) {
		DEBUG("trap_log_delimiter is EMPTY");
		trap_log_delimiter = ",";
	} else {
		DEBUG("trap_log_delimiter: %s", trap_log_delimiter);
	}

	if ((trap_log_handler = fopen(config_get_option_value(":trap_log"), "a")) == NULL)
		log_critical(errno, "cannot open trap log");

	bogomips = get_bogomips();
	DEBUG("bogomips: %f", bogomips);

	trap_rate_threshold = bogomips * atof(config_get_option_value(":trap_rate_threshold"));
	if (!trap_rate_threshold)
		trap_rate_threshold = .001;
	
	DEBUG("trap_rate_threshold: %f", trap_rate_threshold);
	
	startup_time = time(NULL);

	DEBUG("startup_time: %u", (unsigned) startup_time);
}



/*
 *     Callbacks for diagnostics
 *
 ******************************************************************************/


size_t trap_get_trap_log_size(void)
{
	return get_file_size(trap_log_handler);
}
