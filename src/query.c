/*
 *     N a g i o s   S N M P   T r a p   D a e m o n 
 *
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/


/*
 *     query.c --- query parsing
 *
 ******************************************************************************
 ******************************************************************************/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pcre.h>
#include <errno.h>

#include "nagiostrapd.h"



/*
 *     Global declarations
 *
 ******************************************************************************/


#define Q_NAME_SIZE 256 /* max size of a query name */

#define OVECSIZE 30 /* for regex subexpressions */

struct query_t {
	char *q_name;
	char *q_body;
	struct query_t *q_next;
};

static struct query_t *query_table, *qt_tail;


/*
 *     Private methods
 *
 ******************************************************************************/


static void query_table_init(void)
{
	query_table = NULL;
	qt_tail = NULL;
}


static void query_store(const char *name, const char *body)
{
	struct query_t *q_item;

	if (is_empty(name)) {
		DEBUG("called on empty name");
		return;
	}
	if (is_empty(body)) {
		DEBUG("called on empty body");
		return;
	}

	DEBUG("storing query %s: %s", name, body);

	q_item = malloc(sizeof *q_item);
	q_item->q_name = strdup(name);
	q_item->q_body = strdup(body);
	q_item->q_next = NULL;

	if (query_table == NULL || qt_tail == NULL) {
		query_table = q_item;
		qt_tail = q_item;
	} else {
		qt_tail->q_next = q_item;
		qt_tail = q_item;
	}
}


typedef enum {
	Q_OUTSIDE_QUERY, Q_INSIDE_QUERY, Q_INVALID
} q_parse_status_t;


static int query_parse(const char *query_source_file)
{
	char query_begin_marker[] = "-- BEGIN QUERY ([a-zA-Z0-9_]+) --";
	char query_end_marker[] = "-- END QUERY --";
	
	pcre* query_begin_marker_re;
	pcre* query_end_marker_re;

	FILE *query_source_h;


	query_begin_marker_re = regex_compile(query_begin_marker);
	query_end_marker_re = regex_compile(query_end_marker);

	if ((query_source_h = fopen(query_source_file, "r")) != NULL) {

		char buffer[TMPBUFLEN];
		char *line;
		size_t len;
		q_parse_status_t status = Q_OUTSIDE_QUERY;
		int q_found = 0;
		char q_name[Q_NAME_SIZE];
		char *q_body = NULL;
		int q_body_len = 0;
		int ovector[OVECSIZE];
		int rc;

		while ((line = fgets(buffer, sizeof(buffer), query_source_h)) != NULL) {
	
			line = remove_chars(line, "\n\r");

			if (line != NULL && (len = strlen(line)) > 0) {
				switch(status) {
					case Q_OUTSIDE_QUERY:
						if ((rc = regex_execute(query_begin_marker_re, line, ovector, OVECSIZE)) >= 0) {
							pcre_copy_substring(line, ovector, rc, 1, q_name, Q_NAME_SIZE); 

							status = Q_INSIDE_QUERY;
						}
						break;
					case Q_INSIDE_QUERY:
						if (regex_execute(query_end_marker_re, line, ovector, OVECSIZE) >= 0) {
							if (q_found > 0) {
								query_store(q_name, remove_spaces(q_body));
								if (q_body_len > 0)
									q_body[0] = '\0';
								q_body_len = 0;
								q_found = 0;
							}
							status = Q_OUTSIDE_QUERY;
						} else {
							q_body = realloc(q_body, q_body_len + len + 1);
							if (q_body_len > 0)
								q_body[q_body_len-1] = ' ';
							memcpy(q_body + q_body_len, line, len);
							q_body[q_body_len + len] = '\0';
							q_body_len += len + 1;
							q_found = 1;
						}
						break;
					default:
						break;
				}


			}
		}

		fclose(query_source_h);

	} else {
		log_critical(errno, "cannot open query file at specified location %s", query_source_file);
		return -1;
	}

	return 0;
}



/*
 *     Public methods
 *
 ******************************************************************************/


char *query_fetch(const char *name)
{
	struct query_t *q_item;

	if (query_table == NULL)
		return NULL;
	
	for (q_item = query_table; q_item != NULL; q_item = q_item->q_next) {
		if (q_item->q_name != NULL && strcmp(q_item->q_name, name) == 0) {
			if (q_item->q_body != NULL) {
				return q_item->q_body;
			}
		}
	}

	return NULL;
}


/*
 *     Constructor
 *
 ******************************************************************************/

void query_init(void)
{
	char *query_source_file;

	query_source_file = config_get_option_value(":query_source_file");
	query_table_init();

	query_parse(query_source_file);
}


/*
 *     Debug
 *
 ******************************************************************************/


#ifndef NDEBUG

void query_list(void)
{
	struct query_t *q_item;

	if (query_table == NULL)
		return;
	
	for (q_item = query_table; q_item != NULL; q_item = q_item->q_next) {
		printf("Query Name: %s\n", q_item->q_name != NULL ? q_item->q_name : "(null)");
		printf("Query Body: %s\n", q_item->q_body != NULL ? q_item->q_body : "(null)");
	}
}

#endif /* NDEBUG */

