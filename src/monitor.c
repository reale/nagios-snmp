/*
 *     N a g i o s   S N M P   T r a p   D a e m o n 
 *
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/


/*
 *     monitor.c --- ``monitor'' or ``master'' incarnation
 *
 ******************************************************************************
 ******************************************************************************/



#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <assert.h>

#include "nagiostrapd.h"



/*
 *     Global declarations
 *
 ******************************************************************************/


volatile sig_atomic_t monitor_must_terminate = 0;

/* holds pids of all children */
static pid_t children[MAX_WORKERS];

/* how many workers */
static int workers;

/* true if we, as a process, are the monitor */
static int i_am_monitor = 0;



/*
 *     Private methods
 *
 ******************************************************************************/


static void signal_all_children(int signal)
{
	int i;
	for (i = 0; i < workers; i++) {
		kill(children[i], signal);
	}
}


static void terminate(void)
{
	monitor_must_terminate = 1;

	/* shutdown children */
	monitor_kill_children();

	/* remove pid file */
	pidfile_erase();

	/* close the primary socket */
	daemon_close_socket();

	diagnostics_pool_clear();

	exit(0); // XXX
}


static void catch_signal(int signal)
{
	switch (signal) {
		case SIGTERM:
			terminate();
			break;
		case SIGHUP:
			signal_all_children(SIGHUP);
			break;
		default:
			break;
	}
}



/*
 *     Public methods
 *
 ******************************************************************************/


void monitor_init(void)
{
	i_am_monitor = 1;

	/* it's our responsibility to write the pid file */
	pidfile_write();

	/* startup is now completed */
	startup_set_completed(1);

	/* close standard streams */
	fclose(stdin);
	fclose(stdout);
	fclose(stderr);

	/* set signal handlers */
	signal(SIGTERM, catch_signal);
	signal(SIGHUP, catch_signal);
}


void monitor_start_main_loop(void)
{
	while (1) {
		if (monitor_must_terminate)
			break;

		sleep(1);
	}
}


void monitor_register_pid(pid_t my_pid, int my_workers)
{
	assert(my_workers >= 0 && my_workers < MAX_WORKERS);
	children[my_workers] = my_pid;
	workers = my_workers + 1;
}


void monitor_kill_children(void)
{
	if (i_am_monitor)
		signal_all_children(SIGTERM);
}
