/*
 *     N a g i o s   S N M P   T r a p   D a e m o n 
 *
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/


/*
 *     exec.c --- execute trap
 *
 ******************************************************************************
 ******************************************************************************/



#define _GNU_SOURCE /* for getline() */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include "nagiostrapd.h"



/*
 *     Global declarations
 *
 ******************************************************************************/



static int stack_max_executions = 0;


struct buffer_t {
	char *line;
	ssize_t len;
	struct buffer_t *next;
};


struct execitem_t {
	char *command;
	int is_service;
	struct host_t *host;
	struct svc_t *svc;
};


struct exec_result_t {
	int is_complete;
	int is_service;
	int use_sender_address;
	unsigned int timestamp;
	char *host_address;
	char *host_name;
	char *svc_desc;
	int ret_value;
	char *output;
	char *perfdata;
	int is_valid_svc_id;
	int svc_id;
};



/*
 *     Static methods
 *
 ******************************************************************************/


static void free_execitem(void *ptr)
{
	struct execitem_t *execitem = ptr;
	if (ptr == NULL) {
		DEBUG("called on NULL pointer");
		return;
	}

	DEBUG("called");

	free(execitem->command);
	free(execitem);

	DEBUG("done");
}


static struct buffer_t *alloc_buffer(void)
{
	DEBUG("called");
	return xmalloc(sizeof(struct buffer_t));
}


void free_buffer(struct buffer_t *buffer)
{
	struct buffer_t *next;

	if (buffer == NULL) {
		DEBUG("called on NULL pointer");
		return;
	}

	DEBUG("called");

	while (buffer != NULL) {
		DEBUG("freeing buffer line");
		next = buffer->next;
		free(buffer->line);
		free(buffer);
		buffer = next;
	}

	DEBUG("done");
}


static struct buffer_t *file_read_line_by_line(FILE *stream)
{
	struct buffer_t *buffer_head = NULL, *buffer = NULL;

	char *line = NULL;
	size_t len = 0;
	ssize_t read;
#ifndef NDEBUG
	int count_lines = 0;
#endif

	DEBUG("called");

	while ((read = getline(&line, &len, stream)) != -1) {
#ifndef NDEBUG
		count_lines++;
		DEBUG("read line %d: %s (len: %d)", count_lines, line, read);
#endif

		/* add a line to the buffer list */
		if (buffer == NULL) {
			buffer = alloc_buffer();
			buffer_head = buffer;
		} else if (buffer->next == NULL) {
			buffer->next = alloc_buffer();
			buffer = buffer->next;
		} else {
			log_error(0, "error in %s", __func__);
			free_buffer(buffer);
			free(line);
			return NULL;
		}

		buffer->line = line;
		buffer->len = read;
		buffer->next = NULL;
		line = NULL;
	}

	DEBUG("done");

	return buffer_head;
}


/*
 * exec command and put output into a buffer
 */

static struct buffer_t *exec_command(const char *command, const char *trap_contents)
{
	FILE *pipe;
	struct buffer_t *buffer;
	size_t command_len, trap_contents_len;
	char *full_command;

	command_len = xstrlen(command);
	trap_contents_len = xstrlen(trap_contents);

	if (!command_len) {
		DEBUG("called on empty COMMAND");
		return NULL;
	}
	if (!trap_contents_len) {
		DEBUG("called on empty TRAP_CONTENTS");
		return NULL;
	}

	full_command = xmalloc(command_len + trap_contents_len + 2);
	memcpy(full_command, command, command_len);
	full_command[command_len] = ' ';
	memcpy(full_command+command_len+1, trap_contents, trap_contents_len);
	full_command[command_len + trap_contents_len + 1] = '\0';

	DEBUG("full command: %s", full_command);

	if ((pipe = popen(full_command, "r")) == NULL) {
		log_error(errno, "cannot open pipe to command %s", full_command);
		return NULL;
	}

	buffer = file_read_line_by_line(pipe);

	pclose(pipe);

	free(full_command);

	DEBUG("done");

	return buffer;
}


/*
 * parse result string
 */

#if 0
#define RESULT_FIELD_IS_EMPTY(x) (!strcmp(x, "-"))

static void parse_result_field(char **dest, const char *source)
{
	DEBUG("called on source %s", source);

	if (source == NULL || RESULT_FIELD_IS_EMPTY(source)) {
		*dest = xmalloc(1);
		*dest[0] = '\0';
	} else {
		*dest = xstrdup(source);
	}
}
#endif


typedef enum {
	S_STARTED, S_HOST_ADDRESS, S_HOST_NAME, S_SVC_DESC, S_RET_VALUE, S_OUTPUT, S_PERFDATA, S_SVC_ID
} parse_result_str_status_t;

static struct exec_result_t *parse_result_str(char *result_str, int is_service, struct host_t *host, struct svc_t *svc, int use_sender_address)
{
	struct exec_result_t *result;

	char *host_address = NULL;
	char *host_name = NULL;
	char *svc_desc = NULL;
	char *ret_value = NULL;
	char *output = NULL;
	char *perfdata = NULL;
	char *svc_id = NULL;

	char *token;
	parse_result_str_status_t status = S_STARTED;

	result_str = remove_chars(result_str, "\n\r");

	if (is_empty(result_str)) {
		DEBUG("called on empty RESULT_STR");
		return NULL;
	}

	DEBUG("called on RESULT_STR: %s", result_str);

	while ((token = strsep(&result_str, "|")) != NULL) {

		DEBUG("token: %s", token);

		switch(status) {
			case S_STARTED:
				host_address = token;
				status = S_HOST_ADDRESS;
				DEBUG("status: S_HOST_ADDRESS, host_address: %s", host_address);
				break;
			case S_HOST_ADDRESS:
				host_name = token;
				status = S_HOST_NAME;
				DEBUG("status: S_HOST_NAME, host_name: %s", host_name);
				break;
			case S_HOST_NAME:
				svc_desc = token;
				status = S_SVC_DESC;
				DEBUG("status: S_HOST_ADDRESS, svc_desc: %s", svc_desc);
				break;
			case S_SVC_DESC:
				ret_value = token;
				status = S_RET_VALUE;
				DEBUG("status: S_RET_VALUE, ret_value: %s", ret_value);
				break;
			case S_RET_VALUE:
				output = token;
				status = S_OUTPUT;
				DEBUG("status: S_OUTPUT, output: %s", output);
				break;
			case S_OUTPUT:
				perfdata = token;
				status = S_PERFDATA;
				DEBUG("status: S_PERFDATA, perfdata: %s", perfdata);
				break;
			case S_PERFDATA:
				svc_id = token;
				status = S_SVC_ID;
				DEBUG("status: S_SVC_ID, svc_id: %s", svc_id);
				break;
			default:
				break;
		}
	}

	DEBUG("parsing completed");

	result = xmalloc(sizeof *result);

	result->host_address = xstrdup(host_address);
	result->host_name = xstrdup(host_name);
	result->svc_desc = xstrdup(svc_desc);
	result->output = xstrdup(output);
	result->perfdata = xstrdup(perfdata);

	result->is_complete = is_integer(ret_value);
	result->ret_value = atoi(ret_value);

	result->is_valid_svc_id = is_integer(svc_id);
	result->svc_id = atoi(svc_id);

	result->is_service = is_service;

	if (is_service == -1) {
		if (!is_empty(result->svc_desc)) {
			/*
			 * SPECIAL CASE
			 *
			 * here we don't know in advance whether we are confronted with
			 * a host or a service, so we let the plugin choose for us
			 */
			DEBUG("is_service is MAYBE (i.e., -1) and the plugin returned non-empty SVC-DESC, setting is_service := true");
			result->is_service = 1;
		} else {
			DEBUG("is_service is MAYBE (i.e., -1), setting is_service := false");
			result->is_service = 0;
		}
	}

	result->use_sender_address = use_sender_address;

	if (result->is_complete) {
		if (!result->is_service) {
			if (is_empty(result->host_address)) {
				if (host != NULL) {
					char *host_address = db_extract_host_address(host, NULL);
					if (!is_empty(host_address))
						result->host_address = xstrdup(host_address);
				}
			}
			if (is_empty(result->host_name)) {
				if (host != NULL) {
					char *host_name = db_extract_host_name(host, NULL);
					if (!is_empty(host_name))
						result->host_name = xstrdup(host_name);
				}
			}
		} else {
			if (is_empty(result->host_address)) {
				if (svc != NULL) {
					char *host_address = db_extract_host_address(NULL, svc);
					if (!is_empty(host_address))
						result->host_address = xstrdup(host_address);
				}
			}
			if (is_empty(result->host_name)) {
				if (svc != NULL) {
					char *host_name = db_extract_host_name(NULL, svc);
					if (!is_empty(host_name))
						result->host_name = xstrdup(host_name);
				}
			}
			if (is_empty(svc_desc)) {
				if (svc != NULL) {
					char *svc_desc = db_extract_svc_desc(svc);
					if (!is_empty(svc_desc))
						result->svc_desc = xstrdup(svc_desc);
				}
			}
		}
	}


	free(result_str);

	return result;
}


/*
 * push execitem and execlist
 */

static int push_execitem(struct stack_t *stack, const char *command, int is_service, struct host_t *host, struct svc_t *svc)
{
	struct execitem_t *item;

	if (stack == NULL || command == NULL)
		return 1;
	
	item = xmalloc(sizeof *item);

	item->command = xstrdup(command);

	item->is_service = is_service;

	item->host = host;
	
	item->svc = svc;

	return stack_push(stack, item);
}


static int push_execlist(struct stack_t *stack, struct execlist_t *execlist)
{
	struct execlist_t *execlist_current = NULL;

	if (stack == NULL)
		return 1;
	
	while ((execlist_current = execlist_getnext(execlist, execlist_current)) != NULL) {
		char *command;
		int is_service;
		struct host_t *host;
		struct svc_t *svc;

		command = execlist_extract_command(execlist_current);
		if (command == NULL) {
			DEBUG("cannot extract command from execlist");
			return 0;
		}

		host = execlist_extract_host(execlist_current);
		if (host == NULL)
			DEBUG("cannot extract host from execlist");

		svc = execlist_extract_svc(execlist_current);
		if (svc == NULL)
			DEBUG("cannot extract svc from execlist");

		if (!execlist_extract_is_service(execlist_current, &is_service)) {
			DEBUG("cannot extract \"is_service\" from execlist");
			return 0;
		}

		if (!push_execitem(stack, command, is_service, host, svc)) {
			DEBUG("cannot push item onto stack");
			return 0;
		}
	}

	return 1;
}


/*
 * stack run
 */

static struct exec_result_t *stack_run(struct stack_t *stack, const char *command, int cmd_id, int use_sender_address, const char *sender_address, const char *trap_contents, unsigned int trap_timestamp)
{
	struct stack_item_t *stack_item;
	struct exec_result_t *exec_result = NULL;
	struct execitem_t *execitem;
	struct execlist_t *execlist = NULL;
	struct buffer_t *buffer;
	int return_null = 0;

	if (stack_count_executions(stack) > stack_max_executions)
		return NULL;

	stack_increment_executions(stack);

	stack_item = stack_pop(stack);
	execitem = (struct execitem_t *) stack_get_data(stack_item);
	
	buffer = exec_command(execitem->command, trap_contents);

	while (buffer != NULL) {
		exec_result = parse_result_str(buffer->line, execitem->is_service, execitem->host, execitem->svc, use_sender_address);
		if (exec_result->is_complete) {
			break;
		} else {
			if ((execlist = execlist_build(command, cmd_id, use_sender_address, sender_address, exec_result->host_address, exec_result->host_name)) == NULL) {
				return_null = 1;
				break;
			} else {
				if (!push_execlist(stack, execlist)) {
					return_null = 1;
					break;
				}
			}
		}

		buffer = buffer->next;
	}

	free_execitem(execitem);
	free_buffer(buffer);
	execlist_destroy(execlist);

	if (exec_result)
		exec_result->timestamp = trap_timestamp;

	return (return_null ? NULL : exec_result);
}


static struct exec_result_t *prepare_for_output(struct exec_result_t *exec_result)
{
	return exec_result;
}


static void free_exec_result(struct exec_result_t *result)
{
	if (result == NULL)
		return;
	
	free(result->host_address);
	free(result->host_name);
	free(result->svc_desc);
	free(result->output);
	free(result->perfdata);

	free(result);
}


static int do_output(struct exec_result_t *exec_result)
{
	int output_confirmed = 1;

	if (exec_result == NULL) {
		DEBUG("called on NULL exec_result");
		return 0;
	}

	DEBUG("called");

	if (!exec_result->use_sender_address) {

		output_confirmed = 0;

		if (!exec_result->is_service) {
			/* HOST */
			DEBUG("HOST");
			if (db_lookup_host_by_host_name(exec_result->host_name) != NULL
				|| db_lookup_host_by_address(exec_result->host_address) != NULL)
			{
				output_confirmed = 1;
				DEBUG("output confirmed ...");
			}
		} else {
			/* SERVICE */
			DEBUG("SERVICE");
			struct svc_t *svc;
			svc = db_lookup_svc_by_host_name(exec_result->host_name, NULL, 0);
			while (svc != NULL) {
				if (strcmp(exec_result->svc_desc, db_extract_svc_desc(svc)) == 0) {
					output_confirmed = 1;
					break;
				}
				svc = db_lookup_svc_by_host_name(exec_result->host_name, svc, DB_LOOKUP_NEXT_BY_HOST_NAME);
			}
			if (!output_confirmed) {
				svc = db_lookup_svc_by_host_address(exec_result->host_address, NULL, 0);
				while (svc != NULL) {
					if (strcmp(exec_result->svc_desc, db_extract_svc_desc(svc)) == 0) {
						output_confirmed = 1;
						break;
					}
					svc = db_lookup_svc_by_host_address(exec_result->host_address, svc, DB_LOOKUP_NEXT_BY_HOST_ADDRESS);
				}
			}
		}
	}


	if (is_empty(exec_result->host_name)) {
		DEBUG("host_name is empty...");
		if (is_empty(exec_result->host_address))
			DEBUG("host_address is empty as well...");
		output_confirmed = 0;
	}
	if (exec_result->is_service && is_empty(exec_result->svc_desc)) {
		DEBUG("svc_desc is empty...");
		output_confirmed = 0;
	}

	if (output_confirmed) {
		DEBUG("output confirmed!");


		channel_write(
			exec_result->timestamp,
			exec_result->host_name,
			exec_result->svc_desc,
			exec_result->ret_value,
			exec_result->output,
			exec_result->perfdata,
			exec_result->is_service);
	} else {
		DEBUG("output NOT confirmed!");
		return 0;
	}


	free_exec_result(exec_result);
	return 1;
}



/*
 *     Public methods
 *
 ******************************************************************************/


int exec_trap(struct trap_t *trap, const char *oid, const char *command)
{
	char *trap_oid = NULL;
	int cmd_id = 0;
	struct execlist_t *execlist = NULL;
	struct exec_result_t *exec_result;
	struct stack_t *stack;
	char *trap_contents;
	unsigned int trap_timestamp;
	char *filename;
	int should_free_filename = 0;
	int use_sender_address;
	char *sender_address;

	if (trap == NULL) {
		DEBUG("called on NULL trap");
		return 0;
	}

	trap_oid = trap_get_oid(trap);
	if (is_empty(trap_oid)) {
		DEBUG("called on trap with empty OID");
		return 0;
	}

	if (oid != NULL && strcmp(oid, trap_oid)) {
		DEBUG("trap OID does not match OID passed by user");
		return 0;
	}

	if (command == NULL) {
		DEBUG("command is NULL, will lookup stuff from hash tables");

		if ((cmd_id = db_lookup_cmd_id_by_oid(trap_oid)) == -1) {
			DEBUG("cannot lookup command by trap_oid: %s", trap_oid);
			return 0;
		} else {
			DEBUG("found cmd id %d for trap oid %s", cmd_id, trap_oid);
		}

		if ((filename = db_lookup_filename_by_cmd_id(cmd_id)) == NULL) {
			DEBUG("cannot lookup filename by trap_oid: %s", trap_oid);
			return 0;
		} else {
			DEBUG("found filename %s for trap oid %s", filename, trap_oid);
		}

		if (!db_lookup_use_sender_address_by_cmd_id(cmd_id, &use_sender_address)) {
			DEBUG("cannot find whether we must use sender address for trap_oid: %s", trap_oid);
			return 0;
		} else {
			DEBUG("we %s to use sender address for trap_oid %s",
				(use_sender_address ? "NEED" : "DO NOT need"), trap_oid);
		}

	} else {
		DEBUG("command is not NULL");

		if ((filename = extract_filename(command)) == NULL) {
			DEBUG("cannot extract filename from command %s passed by the user", command);
			return 0;
		}

		should_free_filename = 1;
		use_sender_address = 0;
	}

	if (!set_permissions(filename)) {
		DEBUG("cannot set exec permissions on file %s", filename);
		return 0;
	}

	if ((trap_contents = trap_get_contents(trap)) == NULL) {
		DEBUG("cannot extract trap contents from trap");
		return 0;
	} else {
		DEBUG("successfully extracted trap contents from trap: %s", trap_contents);
	}

	if ((trap_timestamp = trap_get_timestamp(trap)) == 0) {
		DEBUG("cannot extract trap timestamp from trap");
		return 0;
	} else {
		DEBUG("successfully extracted trap timestamp from trap: %u", trap_timestamp);
	}

	if ((sender_address = trap_get_sender_address(trap)) == NULL) {
		DEBUG("cannot extract sender address from trap");
		return 0;
	} else {
		DEBUG("successfully extracted sender address %s from trap", sender_address);
	}

	if ((stack = stack_init(free_execitem)) == NULL) {
		log_error(0, "cannot init stack");
		return 0;
	} else {
		DEBUG("stack successfully initialized");
	}

	if (command == NULL) {
		DEBUG("command is NULL");

		if ((execlist = execlist_build(NULL, cmd_id, use_sender_address, sender_address, NULL, NULL)) == NULL) {
			DEBUG("built empty execlist...");
			return 0;
		} else {
			if (!push_execlist(stack, execlist)) {
				DEBUG("cannot push execlist onto stack");
				return 0;
			}
		}
	} else {
		DEBUG("command is not NULL");

		if (!push_execitem(stack, command, 0, NULL, NULL)) {
			DEBUG("cannot push execitem onto stack");
			return 0;
		}
	}

	while (stack_get_depth(stack) > 0) {

		exec_result = stack_run(stack, command, cmd_id, use_sender_address, sender_address, trap_contents, trap_timestamp);
		if (exec_result == NULL)
			return 0;

		if (!prepare_for_output(exec_result))
			return 0;

		do_output(exec_result);
	}

	/*
	 * free everything
	 */
	free(trap_contents);
	execlist_destroy(execlist);
	stack_free(stack);
	if (should_free_filename)
		free(filename);

	return 1;
}



/*
 *     Class constructor
 *
 ******************************************************************************/


void exec_init(void)
{
	stack_max_executions = atoi(config_get_option_value(":plugins_max_executions"));
}
