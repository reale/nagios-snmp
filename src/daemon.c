/*
 *     N a g i o s   S N M P   T r a p   D a e m o n 
 *
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/


/*
 *     daemon.c --- daemon starting code
 *
 ******************************************************************************
 ******************************************************************************/



#define _BSD_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>


#include "nagiostrapd.h"



/*
 *     Global declarations
 *
 ******************************************************************************/


/* server socket descriptor */
static unsigned int server_sckt = 0;



/*
 *     Public methods
 *
 ******************************************************************************/


unsigned int daemon_get_socket(void)
{
	return server_sckt;
}


void daemon_close_socket(void)
{
	DEBUG("called");
	close(server_sckt);
}



/*
 *     Init daemon
 *
 ******************************************************************************/


void daemon_init(uid_t uid, gid_t gid)
{
	int daemonize = 1;
	int enable_monitor = 1;

	/* generic file descriptor */
	int fd;

	/* return value from fork() */
	int res;

	/* number of workers */
	int workers;


	DEBUG("called");

	/* are we supposed to run as a daemon? */
	daemonize = !strcmp(config_get_option_value(":daemonize"), "true");

	/* enable monitor? */
	enable_monitor = !strcmp(config_get_option_value(":enable_monitor"), "true");

	if (daemonize) {

		DEBUG("daemonizing");

		/* ignore terminal control signals, so that we can perform
		   output before detaching */
	#ifdef SIGTTIN
		signal(SIGTTIN, SIG_IGN);
	#endif
	#ifdef SIGTSTP
		signal(SIGTSTP, SIG_IGN);
	#endif

		/* fork a first time */
		if ((res = fork()) < 0)
			log_critical(errno, "cannot fork()");
		else if (res > 0)
			exit(EXIT_SUCCESS);

		DEBUG("forked a first time");

		/* change process group */
		if (setpgrp(0, getpid()) < 0)
			log_critical(errno, "cannot setpgrp()");

		DEBUG("changed process group");

		/* lose controlling terminal */
		if ((fd = open("/dev/tty", O_RDWR)) >= 0) {
			ioctl(fd, TIOCNOTTY, 0);
			close(fd);
		}

		DEBUG("detached from terminal");

		/* reset cwd and umask */
		if (chdir("/") < 0)
			log_error(errno, "cannot chdir('/')");
		umask(0);

		/* fork a second time */
		signal(SIGHUP, SIG_IGN);
		if ((res = fork()) < 0)
			log_critical(errno, "cannot fork()");
		else if (res > 0) {
			exit(EXIT_SUCCESS);
		}

		DEBUG("forked a second time");
	}

	/* create a new socket */
	server_sckt = socket_create(atoi(config_get_option_value(":port_number")),
		!strcmp(config_get_option_value(":socket_reuse"), "true"),
		!strcmp(config_get_option_value(":localhost_only"), "true"));

	/* set non-blocking */
	socket_set_nonblocking(server_sckt);

	if (enable_monitor) {
		/* create workers() */
		workers = atoi(config_get_option_value(":workers"));

		if (workers < 1)
			workers = 1;
		if (workers > MAX_WORKERS)
			workers = MAX_WORKERS;

		int i;

		for (i = 0; i < workers; i++) {
			if ((res = fork()) < 0)
				log_critical(errno, "cannot fork()");
			else if (res > 0)
				/* we are the parent */
				monitor_register_pid(res, i);
			else {
				/* we are the children, i.e, the workers (``slaves'')... */
				worker_init(i, uid, gid);
				worker_start_main_loop();
				return;
			}
		}
	} else {
		worker_init(0, uid, gid);
		worker_start_main_loop();
	}

	/* from now on we are the parent (i.e., the monitor) */

	/* we don't need to hold open the primary socket */
	close(server_sckt);

	monitor_init();
	monitor_start_main_loop();

	return;
}

