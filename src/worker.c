/*
 *     N a g i o s   S N M P   T r a p   D a e m o n 
 *
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/


/*
 *     worker.c --- worker (``slave'') implementation
 *
 ******************************************************************************
 ******************************************************************************/



#define _BSD_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/un.h>
#include <errno.h>

#include "nagiostrapd.h"
#include "threadpool.h"



/*
 *     Global declarations
 *
 ******************************************************************************/


/* number of slots in thread pool */
static int threadpool_size = 0;

/* thread pool */
struct threadpool *pool = NULL;

/* primary socket */
static unsigned int server_sckt = 0;

/* shall we terminate? */
volatile sig_atomic_t worker_must_terminate = 0;

/* our id as a worker */
static int worker_id = -1;

/* private socket descriptor */
static unsigned int private_s = 0;

/* do we write on socket? */
static int send_enabled = 1;

/* holds data passed to child thread */
struct child_data_t {
	unsigned int client_s;
	int is_private;
};



/*
 *     Signal handling stuff
 *
 *     Note: SIGHUP means dump diagnostics
 *
 ******************************************************************************/


/*
 *  terminate
 */

static void terminate(void)
{
	worker_must_terminate = 1;

	/* stop all threads */
	if (threadpool_size > 0)
		threadpool_free(pool, 1);
	
	/* close the primary socket (maybe already closed...) */
	close(server_sckt);
}


/*
 * dump diagnostics
 */

static void dump_diagnostics(void)
{
	diagnostics_write();
}


/*
 * signal handler
 */

static void catch_signal(int signal)
{
	switch (signal) {
		case SIGTERM:
			terminate();
			break;
		case SIGHUP:
			dump_diagnostics();
			break;
		default:
			break;
	}
}



/*
 *     Child thread
 *
 ******************************************************************************/


static void child_thread(void *arg)
{
	struct child_data_t *child_data;

	/* ad-hoc protocol data unit */
	struct pdu_t *pdu;

	/* dynamically allocated buffer */
	char *buffer;

	/* response */
	char *response;

	
	child_data = (struct child_data_t *) arg;

	DEBUG("thread started");

	buffer = socket_read_pdu(child_data->client_s);

	if (buffer) {
		/* process pdu */
		pdu = trap_pdu_process(buffer, child_data->is_private);

		/* get response */
		if ((response = trap_pdu_get_response(pdu)) != NULL) {
			struct trap_t *trap;
		
			DEBUG("sending response: %s", response);

			/* send response */
			if (send_enabled) {
				send(child_data->client_s, response, strlen(response), 0);
				send(child_data->client_s, "\n", 1, 0);
			}

			/* free response */
			free(response);

			/* log & exec trap */
			if ((trap = trap_is_trap(pdu)) != NULL) {
				int executed;
				executed = exec_trap(trap, NULL, NULL);
				traplog_log(trap, executed);
			}
		} else {
			
			DEBUG("response is NULL");
		}

		/* free pdu */
		trap_free_pdu(pdu);

		/* close the client connection */
		close(child_data->client_s);

		/* don't forget to free buffer! */
		free(buffer);
	}

	free(arg);
}


static unsigned int create_private_socket(void)
{
	char *path_prefix;
	char *path;
	unsigned int sock;

	path_prefix = config_get_option_value(":local_socket_prefix");
	path = xmalloc(strlen(path_prefix) + 16);

	sprintf(path, "%s%d", path_prefix, getpid());

	sock = socket_create_unix(path);
	free(path);

	return sock;
}



/*
 *     Worker init
 *
 ******************************************************************************/


void worker_init(int id, uid_t uid, gid_t gid)
{
	/* register our id */
	worker_id = id;

	/* close standard streams */
	fclose(stdin);
	fclose(stdout);
	fclose(stderr);

	/* init diagnostics */
	diagnostics_init();

	/* set signal handlers */
	signal(SIGTERM, catch_signal);
	signal(SIGHUP, catch_signal);

	/* create private socket */
	private_s = create_private_socket();
	socket_set_nonblocking(private_s);
 
	/* get threadpool_size */
	threadpool_size = atoi(config_get_option_value(":threadpool_size"));

	/* create threadpool */
	if (threadpool_size > 0) {
		if ((pool = threadpool_init(threadpool_size)) == NULL)
			log_critical(0, "cannot create thread pool");
	}

	/* change user and group */
	if (uid > 0) {
		if (setuid(uid) < 0)
			log_error(errno, "cannot set UID %d", (int) uid);
	}
	if (gid > 0) {
		if (setgid(gid) < 0)
			log_error(errno, "cannot set GID %d'", (int) gid);
	}
}


int worker_get_id(void)
{
	return worker_id;
}



/*
 *     Worker main loop
 *
 ******************************************************************************/


void worker_start_main_loop(void)
{
	/* client socket descriptor */
	int client_s;

	/* client internet address */
	struct sockaddr_in client_addr;
	struct sockaddr_un client_addr_unix;

	/* internet address length */
	socklen_t addr_len;
	socklen_t addr_len_unix;

        /* socket file descriptors we want to wake up for, using select() */
	fd_set socks;

	int maxfd;


	send_enabled = !strcmp(config_get_option_value(":send_enabled"), "true");

	/* get primary socket */
	server_sckt = daemon_get_socket();

	/* listen for incoming connections */
	if (listen(server_sckt, atoi(config_get_option_value(":pending_connections"))))
		log_critical(errno, "cannot listen()");
	if (listen(private_s, 5))
		log_critical(errno, "cannot listen()");


	addr_len = sizeof(client_addr);
	addr_len_unix = sizeof(client_addr_unix);

	maxfd = max(server_sckt, private_s) + 1;


	/*
	 * the server main loop
	 */

	while (1) {
		struct child_data_t *child_data;
		int is_private = 0;

		client_s = -1;

		DEBUG("server ready ...");  

		if (worker_must_terminate)
			break;

		FD_ZERO(&socks);
		FD_SET(server_sckt, &socks);
		FD_SET(private_s, &socks);

		if (select(maxfd, &socks, NULL, NULL, NULL) < 0 && errno != EINTR)
			log_critical(errno, "select() error");


		if (FD_ISSET(server_sckt, &socks)) {
			DEBUG("new connection on primary socket...");
		
			/* wait for the next client ... */
			client_s = accept(server_sckt, (struct sockaddr *) &client_addr, &addr_len);
		} else if (FD_ISSET(private_s, &socks)) {
			DEBUG("new connection on private socket...");
		
			/* wait for the next client ... */
			client_s = accept(private_s, (struct sockaddr *)&client_addr_unix, &addr_len_unix);

			is_private = 1;
		}


		if (client_s < 0) {
			log_critical(errno, "cannot accept() connection");
		} else {
			DEBUG("accepted new connection");

			child_data = xmalloc(sizeof *child_data);
			child_data->client_s = client_s;
			child_data->is_private = is_private;

			if (threadpool_size > 0) {
				if (threadpool_add_task(pool, child_thread, child_data, 1) != 0)
					log_error(0, "cannot add task to thread pool queue");
			} else {
				child_thread(child_data);
			}

			/* child_data is freed in the child */
			/* free(child_data); */
		}
	}

	kill(getpid(), SIGTERM);

	return;
}



/*
 *     Callbacks for diagnostics
 *
 ******************************************************************************/

unsigned int worker_get_num_of_threads(void)
{
	return threadpool_get_num_of_threads(pool);
}

unsigned int worker_get_length_of_tasks_queue(void)
{
	return threadpool_get_length_of_tasks_queue(pool);
}

unsigned int worker_get_length_of_free_tasks_queue(void)
{
	return threadpool_get_length_of_free_tasks_queue(pool);
}
