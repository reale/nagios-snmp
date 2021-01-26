/*
 *     N a g i o s   S N M P   T r a p   D a e m o n 
 *
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/


/*
 *     diagnostics2.c --- diagnostics support, part 2
 *
 ******************************************************************************
 ******************************************************************************/



#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <ftw.h>
#include <signal.h>
#include <pthread.h>

#include "nagiostrapd.h"



/*
 *     Global declarations
 *
 ******************************************************************************/


/* size of each diagnostics dump file */
#define FILESIZE 51200 /* 50k */

/* a list used to hold values read from a single diagnostics dump file */
struct key_value_list_t {
	char *key;
	char *value;
	struct key_value_list_t *next;
};

/* each item hold the contents of a single diagnostics dump file */
struct parsed_diagnostics_t {
	struct key_value_list_t *key_value_list;
	struct parsed_diagnostics_t *next;
};



/*
 *     Private methods
 *
 ******************************************************************************/


/*
 * split BUFFER into a pair KEY, VALUE
 */

static void split_key_value(char *buffer, char **key, char **value)
{
	char *str, *token, *saveptr, *prev_token = NULL;
	size_t keylen = 0;
	char *keybuf;
	char *valuebuf;

	if (buffer == NULL)
		return;

	keybuf = strdup(buffer);

	for (str = buffer; ; str = NULL) {
		token = strtok_r(str, " ", &saveptr);

		if (token == NULL)
			break;

		keylen += strlen(token) + 1;
		prev_token = token;
	}

	keylen -= strlen(prev_token) + 1;
	keybuf[keylen - 1] = '\0';

	*key = keybuf;

	valuebuf = strdup(prev_token);

	*value = valuebuf;
}


/*
 * read a single diagnostics dump file
 */

static struct key_value_list_t *parse_diagnostics(char *buffer)
{
	char *str, *token, *saveptr;
	struct key_value_list_t *head = NULL, *tail = NULL;

	if (buffer == NULL)
		return NULL;

	for (str = buffer; ; str = NULL) {
		char *key, *value;

		token = strtok_r(str, "\n", &saveptr);

		if (token == NULL)
			break;

		if (tail == NULL) {
			tail = xmalloc(sizeof *tail);
			head = tail;
		} else {
			tail->next = xmalloc(sizeof *tail->next);
			tail = tail->next;
		}

		split_key_value(token, &key, &value);

		tail->key = key;
		tail->value = value;
		tail->next = NULL;
	}

	return head;
}


/*
 * traverse the diagnostics pool and read every file therein
 */

static struct parsed_diagnostics_t *diagnostics_list = NULL;

static int traverse_diagnostics_pool(const char *fpath, __attribute__((unused)) const struct stat *ptr, int flag)
{
	FILE *handler;
	char buffer[FILESIZE];
	static struct parsed_diagnostics_t *tail;

	if (flag == FTW_F) {

		/* fpath is a regular file */
		if ((handler = fopen(fpath, "r")) != NULL) {

			memset(buffer, 0, FILESIZE);

			if (fread(buffer, FILESIZE, 1, handler) < FILESIZE && ferror(handler)) {
				DEBUG("cannot read file %s", fpath);
				return 0;
			} else {
				if (diagnostics_list == NULL) {
					tail = xmalloc(sizeof *tail);
					diagnostics_list = tail;
				} else {
					tail->next = xmalloc(sizeof *tail->next);
					tail = tail->next;
				}

				tail->key_value_list = parse_diagnostics(buffer);
				tail->next = NULL;
			}

			fclose(handler);

		} else {
			DEBUG("cannot open file %s", fpath);
		}
	}

	return 0;
}


/*
 * search a list of KEY, VALUE pairs by KEY
 */

static char *get_value_by_key(struct key_value_list_t *list, char *key)
{
	if (list == NULL)
		return NULL;

	while (list != NULL) {
		if (!strcmp(list->key, key))
			return list->value;
		list = list->next;
	}

	return NULL;
}


/*
 * sum up all diagnostics dump files
 */

static char *get_diagnostics_global(void)
{
	int i;
	char *buffer, *aux;
	size_t buflen = TMPBUFLEN, auxlen, usedlen = 0;
	struct parsed_diagnostics_t *list;
	static int padding = 0;

	if (!padding)
		padding = diagnostics_get_padding();

	buffer = xmalloc(buflen);

	for (i = 0 ;; i++) {
		double global_value = 0;
		char *key;

		key = diagnostics_get_key(i);

		if (!key)
			break;

		if (!strcmp(key, "Pid"))
			continue;

		list = diagnostics_list;

		while (list != NULL) {
			char *value = get_value_by_key(list->key_value_list, key);
			double value_f;

			if (value != NULL) {
				value_f = atof(value);
				if (diagnostics_get_cumulative(i))
					global_value += value_f;
				else
					global_value = value_f;
			}

			list = list->next;
		}

		if (diagnostics_get_integer(i))
			auxlen = asprintf(&aux, "%ld", (long) global_value);
		else
			auxlen = asprintf(&aux, "%0.3f", global_value);

		if (usedlen + padding + auxlen + 1 < buflen) {
			buflen += TMPBUFLEN + padding + auxlen + 1;
			buffer = xrealloc(buffer, buflen);
		}

		memset(buffer+usedlen, 0x20, padding);
		memcpy(buffer+usedlen, key, strlen(key));
		memcpy(buffer+usedlen+padding, aux, auxlen);
		free(aux);
		usedlen += padding + auxlen + 1;
		buffer[usedlen-1] = '\n';
	}

	buffer[usedlen-1] = '\n';
	buffer[usedlen] = '\0';

	return buffer;
}



/*
 *     Public methods
 *
 ******************************************************************************/


void diagnostics2_do(void)
{
	char *buffer;
	int pid;
	char *diagnostics_pool;

	diagnostics_pool = config_get_option_value(":diagnostics_pool");
	if (is_empty(diagnostics_pool)) {
		fprintf(stderr, "diagnostics_pool is NULL, check config file\n");
		exit(EXIT_FAILURE);
	}

	/* read pid file */
	if ((pid = pidfile_read()) <= 1) {
		fprintf(stderr, "invalid pid: %d\n", pid);
		exit(EXIT_FAILURE);
	}

	/* signal MONITOR to start dumping diagnostics */
	if (kill(pid, SIGHUP) < 0) {
		fprintf(stderr, "cannot signal MONITOR: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* wait a little bit... */
	sleep(1);

	/* read all diagnostics dump file */
	ftw(config_get_option_value(":diagnostics_pool"), traverse_diagnostics_pool, 32);

	/* sum up data... */
	buffer = get_diagnostics_global();

	/* print data */
	printf("%s", buffer);

	/* done... */
	free(buffer);
}
