/*
 *     N a g i o s   S N M P   T r a p   D a e m o n 
 *
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/


/*
 *     pidfile.c --- pid file management
 *
 ******************************************************************************
 ******************************************************************************/


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#include "nagiostrapd.h"



/*
 *     Global declarations
 *
 ******************************************************************************/



/* path to the pid file */
static char *pidfile_path = NULL;



/*
 *     Private methods
 *
 ******************************************************************************/


static void get_pidfile(void)
{
	DEBUG("called");

	if (!pidfile_path)
		pidfile_path = config_get_option_value(":pid_file");
	
	if (is_empty(pidfile_path))
		log_critical(0, "pid_file options not set");

	DEBUG("pidfile path is %s", pidfile_path);
}



/*
 *     Public methods
 *
 ******************************************************************************/


/*
 * write pid into pidfile
 */

void pidfile_write(void)
{
	FILE *pidfile;
	pid_t mypid = getpid();

	DEBUG("called");

	get_pidfile();

	if ((pidfile = fopen(pidfile_path, "w")) == NULL)
		log_critical(errno, "cannot open pid file");

	if (fprintf(pidfile, "%d\n", mypid) < 0)
		log_critical(errno, "cannot write my pid into pidfile");

	fclose(pidfile);

	DEBUG("pidfile written");
}


/*
 * read pid from pidfile
 */

pid_t pidfile_read(void)
{
	FILE *pidfile;
	char buffer[32];
	int pid;

	DEBUG("called");

	get_pidfile();

	if ((pidfile = fopen(pidfile_path, "r")) == NULL) {
		fprintf(stderr, "cannot open pidfile %s: %s\n", pidfile_path, strerror(errno));
		fprintf(stderr, "maybe the daemon is not running!\n");
		exit(EXIT_FAILURE);
	}

	if (fscanf(pidfile, "%s\n", buffer) != 1) {
		fprintf(stderr, "cannot read pid file: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	fclose(pidfile);

	pid = atoi(buffer);

	DEBUG("read pid %d", pid);

	return (pid_t) pid;
}


/*
 * erase pidfile
 */

void pidfile_erase(void)
{
	DEBUG("called");

	get_pidfile();

	if (unlink(pidfile_path) < 0)
		log_error(errno, "cannot remove pid file");
	
	DEBUG("done");
}
