/*
 *     N a g i o s   S N M P   T r a p   D a e m o n 
 *
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/


/*
 *     command.c --- processing of nagios macros
 *
 ******************************************************************************
 ******************************************************************************/


#include <stdlib.h>
#include <string.h>
#include "nagiostrapd.h"

#define UNKNOWN_ADDRESS "UNKNOWN"





typedef char *(*expand_hook_t)(const char *, const char *, const char *, const char *, char **, int);

static char *expand_command_internal(expand_hook_t expand_hook, const char *command, const char *host_svc_name, const char *host_address, const char *sender_address, char **args, int args_no)
{
	char *buffer, *macrobuffer, *macrovalue;
	size_t buflen = TMPBUFLEN, macrobuflen = TMPBUFLEN_SMALL;
	enum { S_NORMAL, S_POSSIBLE_MACRO } status;
	char c;
	unsigned int i = 0, j = 0;

	if (command == NULL)
		return NULL;

	buffer = xmalloc(buflen);
	macrobuffer = xmalloc(macrobuflen);

	status = S_NORMAL;

	/* for us, a macro is everything between two $'s */
	while ((c = *command++) != '\0') {
		if (i > buflen-1) /* '-1' is for trailing '\0' */ {
			buflen *= 2;
			buffer = xrealloc(buffer, buflen);
		}
		if (j > macrobuflen-1) /* '-1' is for trailing '\0' */ {
			macrobuflen *= 2;
			macrobuffer = xrealloc(macrobuffer, macrobuflen);
		}

		switch(status) {
			case S_NORMAL:
				if (c == '$') {
					/* macro started... */
					status = S_POSSIBLE_MACRO;
					j = 0;
					macrobuffer[j++] = c;
				} else {
					buffer[i++] = c;
				}
				break;
			case S_POSSIBLE_MACRO:
				if (c == '$') {
					/* macro ended... */
					status = S_NORMAL;
					macrobuffer[j] = c;
					macrobuffer[j+1] = '\0';
					if ((macrovalue = expand_hook(macrobuffer, host_svc_name, host_address, sender_address, args, args_no)) != NULL) {
						/* macro, expand... */
						size_t macrovaluelen = strlen(macrovalue);
						if (i + macrovaluelen > buflen-1) {
							buflen = buflen*2 + macrovaluelen;
							buffer = xrealloc(buffer, buflen);
						}

						memcpy(buffer+i, macrovalue, macrovaluelen);
						free(macrovalue);
						i += macrovaluelen;
					} else {
						/* not a macro, copy literally */
						if (i + j > buflen-1) {
							buflen = buflen*2 + j;
							buffer = xrealloc(buffer, buflen);
						}

						memcpy(buffer+i, macrobuffer, j+1);
						i += j + 1;
					}
				} else {
					macrobuffer[j++] = c;
				}
				break;
			default:
				/* XXX warn user */
				break;
		}
				
	}

	if (status == S_POSSIBLE_MACRO) {
		/* a ``fake'' traling macro, copy literally */
		if (i + j > buflen-1) {
			buflen = buflen*2 + j;
			buffer = xrealloc(buffer, buflen);
		}

		memcpy(buffer+i, macrobuffer, j);
		i += j;
	}

	buffer[i] = '\0';

	free(macrobuffer);

	return buffer;
}

/*
 * expand command - step 1 - substitutes user-defined macros (resources)
 */
static char *_expand_hook_1(
	const char *macro,
	__attribute__((unused)) const char *host_svc_name,
	__attribute__((unused)) const char *host_address,
	__attribute__((unused)) const char *sender_address,
	__attribute__((unused)) char **args, __attribute__((unused)) int args_no)
{
	return db_lookup_resource(macro);
}

char *command_expand_1(const char *command)
{
	expand_hook_t expand_hook = &_expand_hook_1;
	return expand_command_internal(expand_hook, command, NULL, NULL, NULL, NULL, 0);
}

/*
 * expand command - step 2 - substitutes $SENDERADDRESS$ and $HOSTADDRESS$
 */
static char *_expand_hook_2(const char *macro,
	__attribute__((unused)) const char *host_svc_name,
	const char *host_address,
	const char *sender_address,
	__attribute__((unused)) char **args,
	__attribute__((unused)) int args_no)
{
	char *buffer;

	if (strcmp(macro, "$HOSTADDRESS$") == 0) {
		if (host_address != NULL && strlen(host_address) > 0) {
			buffer = xstrdup(host_address);
		} else {
			buffer = xstrdup(UNKNOWN_ADDRESS);
		}
		return buffer;
	}

	if (strcmp(macro, "$SENDERADDRESS$") == 0) {
		if (sender_address != NULL && strlen(sender_address) > 0) {
			buffer = xstrdup(sender_address);
		} else {
			buffer = xstrdup(UNKNOWN_ADDRESS);
		}
		return buffer;
	}

	return NULL;
}

char *command_expand_2(const char *command, const char *host_address, const char *sender_address)
{
	expand_hook_t expand_hook = &_expand_hook_2;
	return expand_command_internal(expand_hook, command, NULL, host_address, sender_address, NULL, 0);
}

/*
 * expand command - step 3 - substitutes command arguments (per host and per service)
 */
static char *_expand_hook_3(const char *macro,
	__attribute__((unused)) const char *host_svc_name,
	const char *host_address,
	__attribute__((unused)) const char *sender_address,
	char **args, int args_no)
{
	char *buffer, n_buffer[32];
	size_t macrolen;
	int arg_index;

	if (strcmp(macro, "$HOSTADDRESS$") == 0) {
		buffer = xstrdup(host_address);
		return buffer;
	}

	if (strcmp(macro, "$SENDERADDRESS$") == 0) {
		buffer = xstrdup(UNKNOWN_ADDRESS);
		return buffer;
	}

	if (memcmp(macro, "$ARG", 4) != 0) {
		/* unknown macro */
		return NULL;
	}

	/* try to expand args */
	if (args == NULL)
		return NULL;

	/* args are of the form $ARGn$, with n an integer */
	macrolen = strlen(macro);
	/* n should begin at position 4 and be MACROLEN-5 chars long */
	memcpy(n_buffer, macro+4, macrolen-5);
	n_buffer[macrolen-5] = '\0';

	arg_index = atoi(n_buffer);
	if (arg_index < 1 || arg_index > args_no)
		return NULL;
	
	if (args[arg_index-1] != NULL) {
		buffer = xstrdup(args[arg_index-1]);
		return buffer;
	} else {
		return NULL;
	}
		
}

char *command_expand_3(const char *command, const char *host_svc_name, const char *host_address, char **args, int args_no)
{
	expand_hook_t expand_hook = &_expand_hook_3;
	return expand_command_internal(expand_hook, command, host_svc_name, host_address, NULL, args, args_no);
}
