/*
 *     N a g i o s   S N M P   T r a p   D a e m o n 
 *
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/


/*
 *     execlist.c --- implements an ``execlist'' data structure
 *
 ******************************************************************************
 ******************************************************************************/



#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>

#include "nagiostrapd.h"



/*
 *     Global declarations
 *
 ******************************************************************************/


/*
 * the execlist struct itself...
 */
struct execlist_t {
	char *command;
	int is_service;
	struct host_t *host;
	struct svc_t *svc;
	struct execlist_t *next;
};


/*
 * diagnostics counters
 */
static long execlist_created = 0;
static long execlist_destroyed = 0;
static long execlist_current = 0;
static pthread_mutex_t execlist_counter_mutex = PTHREAD_MUTEX_INITIALIZER;



/*
 *     Private methods
 *
 ******************************************************************************/


/*
 * append an item to an execlist
 */

static struct execlist_t *execlist_append(struct execlist_t *execlist, struct execlist_t **head, const char *command, int is_service, struct host_t *host, struct svc_t *svc)
{
	struct execlist_t *new;

	DEBUG("called");

	new = xmalloc(sizeof *new);
	new->is_service = is_service;
	new->next = NULL;

	DEBUG("command: %s", is_empty(command) ? "NULL" : command);

	if (host == NULL)
		DEBUG("host is NULL");
	if (svc == NULL)
		DEBUG("svc is NULL");

	new->command = xstrdup(command);
	new->host = host;
	new->svc = svc;

	if (execlist == NULL) {
		DEBUG("this is the first item");
		*head = new;
	} else {
		DEBUG("item appended");
		execlist->next = new;
	}

	DEBUG("done");

	return new;
}


static void increase_created_counter(void)
{
	pthread_mutex_lock(&execlist_counter_mutex);
	execlist_created++;
	execlist_current++;
	pthread_mutex_unlock(&execlist_counter_mutex);
}

static void increase_destroyed_counter(void)
{
	pthread_mutex_lock(&execlist_counter_mutex);
	execlist_destroyed++;
	execlist_current--;
	pthread_mutex_unlock(&execlist_counter_mutex);
}

/*
 *     Public methods
 *
 ******************************************************************************/


/*
 * build an execlist
 */

struct execlist_t *execlist_build(const char *command, int cmd_id, int use_sender_address, const char *sender_address, const char *host_address, const char *host_name)
{
	struct execlist_t *execlist_head = NULL, *execlist_current = NULL;

	DEBUG("called");

	if (!is_empty(command)) {
		char *expanded_text;

		DEBUG("command NOT NULL: %s", command);

		expanded_text = command_expand_2(command, host_address, sender_address);

		DEBUG("expanded text: %s", is_empty(expanded_text) ? "NULL" : expanded_text);

		execlist_current = execlist_append(execlist_current, &execlist_head, expanded_text, 0, NULL, NULL);

		free(expanded_text);
		increase_created_counter();

		return execlist_head;
	}

	assert(command == NULL);

	DEBUG("command is NULL");
	DEBUG("cmd_id: %d", cmd_id);

	/* were we provided with HOST_ADDRESS? */

	if (!is_empty(host_address)) {
		struct host_t *host;
		struct svc_t *svc;

		DEBUG("HOST_ADDRESS not empty! :-)");

		host = db_lookup_host_by_cmd_id(cmd_id, NULL, 0);
		while (host) {

			DEBUG("found host");

			if (strcmp(host_address, db_extract_host_address(host, NULL)) == 0) {
				DEBUG("host matches!!! :-D");
				execlist_current = execlist_append(execlist_current, &execlist_head, db_extract_expanded_text(host, NULL), 0, host, NULL);
			}
			host = db_lookup_host_by_cmd_id(cmd_id, host, DB_LOOKUP_NEXT_BY_HOST_ADDRESS);
		}

		svc = db_lookup_svc_by_cmd_id(cmd_id, NULL, 0);
		while (svc != NULL) {

			DEBUG("found service");

			if (strcmp(host_address, db_extract_host_address(NULL, svc)) == 0) {
				DEBUG("svc matches!!! :-D");
				execlist_current = execlist_append(execlist_current, &execlist_head, db_extract_expanded_text(NULL, svc), 1, NULL, svc);
			}
			svc = db_lookup_svc_by_cmd_id(cmd_id, svc, DB_LOOKUP_NEXT_BY_HOST_ADDRESS);
		}

		/* HOST_ADDRESS has priority */
		increase_created_counter();
		return execlist_head;
	}

	/* were we provided with HOST_NAME? */

	if (host_name != NULL && strlen(host_name) > 0) {
		struct host_t *host;
		struct svc_t *svc;

		DEBUG("HOST_NAME not empty! :-)");

		host = db_lookup_host_by_cmd_id(cmd_id, NULL, 0);
		while (host != NULL) {

			DEBUG("found host");

			if (strcmp(host_name, db_extract_host_name(host, NULL)) == 0) {
				DEBUG("host matches!!! :-D");
				execlist_current = execlist_append(execlist_current, &execlist_head, db_extract_expanded_text(host, NULL), 0, host, NULL);
			}
			host = db_lookup_host_by_cmd_id(cmd_id, host, DB_LOOKUP_NEXT_BY_HOST_NAME);
		}

		svc = db_lookup_svc_by_cmd_id(cmd_id, NULL, 0);
		while (svc != NULL) {

			DEBUG("found service");

			if (strcmp(host_name, db_extract_host_name(NULL, svc)) == 0) {
				DEBUG("svc matches!!! :-D");
				execlist_current = execlist_append(execlist_current, &execlist_head, db_extract_expanded_text(NULL, svc), 1, NULL, svc);
			}
			svc = db_lookup_svc_by_cmd_id(cmd_id, svc, DB_LOOKUP_NEXT_BY_HOST_NAME);
		}

		/* HOST_ADDRESS has 2nd priority */
		increase_created_counter();
		return execlist_head;
	}
	
	/* were we provided with SENDER_ADDRESS? */

	if (sender_address != NULL && strlen(sender_address) > 0) {

		DEBUG("SENDER_ADDRESS not empty! :-)");

		if (use_sender_address) {
			struct host_t *host;
			struct svc_t *svc;

			DEBUG("using sender address");

			host = db_lookup_host_by_cmd_id(cmd_id, NULL, 0);
			while (host != NULL) {

				DEBUG("found host");

				if (strcmp(sender_address, db_extract_host_address(host, NULL)) == 0) {
					DEBUG("host matches!!! :-D");
					execlist_current = execlist_append(execlist_current, &execlist_head, db_extract_expanded_text(host, NULL), 0, host, NULL);
				}
				host = db_lookup_host_by_cmd_id(cmd_id, host, DB_LOOKUP_NEXT_BY_HOST_ADDRESS);
			}

			svc = db_lookup_svc_by_cmd_id(cmd_id, NULL, 0);
			while (svc != NULL) {

				DEBUG("found service");

				if (strcmp(sender_address, db_extract_host_address(NULL, svc)) == 0) {
					DEBUG("svc matches!!! :-D");
					execlist_current = execlist_append(execlist_current, &execlist_head, db_extract_expanded_text(NULL, svc), 1, NULL, svc);
				}
				svc = db_lookup_svc_by_cmd_id(cmd_id, svc, DB_LOOKUP_NEXT_BY_HOST_ADDRESS);
			}

		} else {
			char *expanded_text;

			DEBUG("NOT using sender address");

			expanded_text = command_expand_2(db_lookup_expanded_text_by_cmd_id(cmd_id), NULL, sender_address);
			execlist_current = execlist_append(execlist_current, &execlist_head, expanded_text, -1, NULL, NULL);
			free(expanded_text);
		}

		increase_created_counter();
		return execlist_head;
	}

	return NULL;
}


struct execlist_t *execlist_getnext(struct execlist_t *execlist_head, struct execlist_t *execlist_current)
{
	if (execlist_head == NULL)
		return NULL;
	
	if (execlist_current == NULL)
		return execlist_head;

	return execlist_current->next;
}


char *execlist_extract_command(struct execlist_t *execlist_current)
{
	if (execlist_current == NULL)
		return NULL;
	
	return execlist_current->command;
}


int execlist_extract_is_service(struct execlist_t *execlist_current, int *is_service)
{
	if (execlist_current == NULL)
		return 0;

	*is_service = execlist_current->is_service;
	return 1;
}


struct host_t *execlist_extract_host(struct execlist_t *execlist_current)
{
	if (execlist_current == NULL)
		return NULL;
	
	return execlist_current->host;
}


struct svc_t *execlist_extract_svc(struct execlist_t *execlist_current)
{
	if (execlist_current == NULL)
		return NULL;
	
	return execlist_current->svc;
}


void execlist_destroy(struct execlist_t *execlist)
{
	struct execlist_t *next;

	if (execlist == NULL)
		return;
	
	while (execlist != NULL) {
		next = execlist->next;
		free(execlist->command);
		free(execlist);
		execlist = next;
	}

	increase_destroyed_counter();
}
