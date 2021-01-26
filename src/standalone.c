/*
 *     N a g i o s   S N M P   T r a p   D a e m o n 
 *
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/


/*
 *     standalone.c --- standalone mode
 *
 ******************************************************************************
 ******************************************************************************/



#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "nagiostrapd.h"



/*
 *     Public methods
 *
 ******************************************************************************/


void standalone_init(
#ifdef NDEBUG
	__attribute__((unused))
#endif
	const char *oid, 
#ifdef NDEBUG
	__attribute__((unused))
#endif
	const char *command)
{
	assert(!(is_empty(oid) || is_empty(command)));
}


void standalone_start_main_loop(const char *oid, const char *command)
{
	char *line = NULL;
	size_t len = 0;
	ssize_t read;

	char *trap_handler;
	struct pdu_t *pdu;
	char *serialized_trap;

	char *response;

	trap_handler = command_expand_1(command);

	while ((read = getline(&line, &len, stdin)) != -1) {
		// XXX check for errors

		/* process pdu */
		pdu = trap_pdu_process(line, 1);

		/* get response */
		if ((response = trap_pdu_get_response(pdu)) != NULL) {
			struct trap_t *trap;
		
			/* send response */
			printf("%s\n", response);

			free(response);

			/* log & exec trap */
			if ((trap = trap_is_trap(pdu)) != NULL) {
				int executed;
				executed = exec_trap(trap, oid, trap_handler);
				if ((serialized_trap = trap_serialize(trap, executed, ",")) != NULL) { 
					printf("=== begin serialized trap ===\n");
					printf("%s\n", serialized_trap);
					printf("=== end   serialized trap ===\n");
					free(serialized_trap);
				}
			}
		}

		/* free pdu */
		trap_free_pdu(pdu);

		/* free line */
		free(line);
	}
}
