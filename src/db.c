/*
 *     N a g i o s   S N M P   T r a p   D a e m o n 
 *
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/


/*
 *     db.c --- connection to Nagios3 database
 *
 ******************************************************************************
 ******************************************************************************/



#include <string.h>
#include <glib.h>
#include <my_global.h>
#include <mysql.h>

#include "nagiostrapd.h"



/*
 *     Global declarations
 *
 ******************************************************************************/


#define MAX_ARGS_NO 128

struct db_status_t {
	MYSQL *conn;

	GHashTable *resources_hash_table;
	GHashTable *commands_hash_table;
	GHashTable *hosts_by_host_name_hash_table;
	GHashTable *hosts_by_address_hash_table;
	GHashTable *hosts_by_cmd_id_hash_table;
	GHashTable *svcs_by_host_name_hash_table;
	GHashTable *svcs_by_host_address_hash_table;
	GHashTable *svcs_by_cmd_id_hash_table;
	GHashTable *trap_handlers_hash_table;
};

static struct db_status_t *db;

struct resource_t {
	char *r_name;
	char *r_value;
};

struct command_t {
	int c_id;
	char *c_name;
	char *c_filename;
	char *c_text;
	char *c_expanded_text;
	int c_use_sender_address;
};

struct host_t {
	int h_cmd_id;
	char *h_host_name;
	char *h_address;
	struct command_t *h_trap_command;
	char **h_args;
	int h_args_no;
	char *h_expanded_text;
	struct host_t *h_next_by_name;
	struct host_t *h_next_by_address;
	struct host_t *h_next_by_cmd_id;
};

struct svc_t {
	int svc_cmd_id;
	char *svc_host_name;
	char *svc_host_address;
	char *svc_description;
	struct command_t *svc_trap_command;
	char **svc_args;
	int svc_args_no;
	char *svc_expanded_text;
	struct svc_t *svc_next_by_host_name;
	struct svc_t *svc_next_by_host_address;
	struct svc_t *svc_next_by_cmd_id;
};

struct trap_handler_t {
	char *th_oid;
	struct command_t *th_trap_command;
	struct trap_handler_t *next;
};



/*
 *     Private methods
 *
 ******************************************************************************/


/*
 * error-reporting wrappers to mysql-related functions
 */


static void query(const char *query_name)
{
	char *stmt_str;

	if ((stmt_str = query_fetch(query_name)) == NULL)
		log_critical(0, "cannot find query: %s", query_name);

	if (mysql_query(db->conn, stmt_str))
		log_critical(0, "error in mysql_query(): %s", mysql_error(db->conn));
}


static MYSQL_RES *store_result(void)
{
	MYSQL_RES *res;

	if ((res = mysql_store_result(db->conn)) == NULL)
		log_critical(0, "error in mysql_store_result(): %s", mysql_error(db->conn));
	
	return res;
}


static MYSQL_ROW fetch_row(MYSQL_RES *result)
{
	MYSQL_ROW row;
	const char *err;

	if (!(row = mysql_fetch_row(result))) {
		err = mysql_error(db->conn);

		if (!is_empty(err)) {
			log_critical(0, "error in mysql_fetch_row(): %s", err);
			return NULL;
		}
	}

	return row;
}


static void free_result(MYSQL_RES *result) {
	mysql_free_result(result);
}


/*
 * fetch resource
 */

static int fetch_resources(void)
{
	MYSQL_RES *result;
	MYSQL_ROW row;
	char *name, *value, *saveptr = NULL;
	struct resource_t *resource;
	int count = 0;

	query("Q_FETCH_RESOURCES");
	result = store_result();

	db->resources_hash_table = g_hash_table_new(g_str_hash, g_str_equal);

	while ((row = fetch_row(result))) {

		name = strtok_r(row[0], "=", &saveptr);
		value = strtok_r(NULL, "=", &saveptr);

		resource = xmalloc(sizeof *resource);

		resource->r_name = xstrdup(name);
		resource->r_value = xstrdup(value);

		/* if two resources exist having the same key, we store only the newest one
		   (by default g_hash_table_insert() replaces the current value if already
		   existing) */
		g_hash_table_insert(db->resources_hash_table, resource->r_name, resource);

		DEBUG("fetched resource: %s [%s]", name, value);

		count++;
	}

	DEBUG("=== fetched %d resource(s) ===", count);

	free_result(result);

	return count;
}


/*
 * fetch command
 */

static int fetch_commands(void)
{
	MYSQL_RES *result;
	MYSQL_ROW row;
	struct command_t *command;
	char *expanded_text;
	int count = 0;

	query("Q_FETCH_COMMANDS");
	result = store_result();

	db->commands_hash_table = g_hash_table_new(g_int_hash, g_int_equal);

	while ((row = fetch_row(result))) {
		command = xmalloc(sizeof *command);
		command->c_id = atoi(row[0]);
		command->c_name = xstrdup(row[1]);
		command->c_text = xstrdup(row[2]);

		if (strcmp(row[3], "1") == 0)
			command->c_use_sender_address = 1;
		else
			command->c_use_sender_address = 0;

		expanded_text = command_expand_1(command->c_text);
		if (expanded_text != NULL) {
			command->c_expanded_text = expanded_text;

			command->c_filename = extract_filename(command->c_expanded_text);

			g_hash_table_insert(db->commands_hash_table, &command->c_id, command);

			DEBUG("fetched command: %s [id: %d] [exp: %s] [use_ip: %s]", command->c_text, command->c_id, command->c_expanded_text, bool_p(command->c_use_sender_address));

			count++;

		} else {
			log_critical(0, "error in fetch_command(): cannot expand %s", command->c_text);
		}
	}

	DEBUG("=== fetched %d command(s) ===", count);

	free_result(result);

	return count;
}


/*
 * split args
 */

static int split_args(char *string, char ***args)
{
	char *str, *token, *saveptr, **args_v;
	int j;

	if (string == NULL) {
		*args = NULL;
		return 0;
	}

	args_v = xmalloc(sizeof(char *) * MAX_ARGS_NO);

	for (j = 0, str = string; j < MAX_ARGS_NO; j++, str = NULL) {
		token = strtok_r(str, "!", &saveptr);

		if (token == NULL)
			break;

		args_v[j] = xstrdup(token);
	}

	*args = args_v;
	return j;
}


/*
 * fetch host
 */

static int fetch_hosts(void)
{
	MYSQL_RES *result;
	MYSQL_ROW row;
	int cmd_id;
	struct command_t *command;
	struct host_t *host, *next_by_name, *next_by_address, *next_by_cmd_id;
	int count = 0;

	char *expanded_text;

	query("Q_FETCH_HOSTS");
	result = store_result();

	db->hosts_by_host_name_hash_table = g_hash_table_new(g_str_hash, g_str_equal);
	db->hosts_by_address_hash_table = g_hash_table_new(g_str_hash, g_str_equal);
	db->hosts_by_cmd_id_hash_table = g_hash_table_new(g_int_hash, g_int_equal);

	while ((row = fetch_row(result))) {
		cmd_id = atoi(row[2]);
		command = (struct command_t *) g_hash_table_lookup(db->commands_hash_table, &cmd_id);

		if (command != NULL) {
			host = xmalloc(sizeof *host);
			host->h_cmd_id = cmd_id;
			host->h_host_name = xstrdup(row[0]);
			host->h_address = xstrdup(row[1]);

			host->h_args_no = split_args(row[3], &host->h_args);

			expanded_text = command_expand_3(command->c_expanded_text, host->h_host_name, host->h_address, host->h_args, host->h_args_no);
			if (expanded_text != NULL) {
				host->h_expanded_text = expanded_text;

				if ((next_by_name = g_hash_table_lookup(db->hosts_by_host_name_hash_table, host->h_host_name)) != NULL) {
					g_hash_table_steal(db->hosts_by_host_name_hash_table, host->h_host_name);
					host->h_next_by_name = next_by_name;
				} else {
					host->h_next_by_name = NULL;
				}

				if ((next_by_address = g_hash_table_lookup(db->hosts_by_address_hash_table, host->h_address)) != NULL) {
					g_hash_table_steal(db->hosts_by_address_hash_table, host->h_address);
					host->h_next_by_address = next_by_address;
				} else {
					host->h_next_by_address = NULL;
				}

				if ((next_by_cmd_id = g_hash_table_lookup(db->hosts_by_cmd_id_hash_table, &cmd_id)) != NULL) {
					g_hash_table_steal(db->hosts_by_cmd_id_hash_table, &cmd_id);
					host->h_next_by_cmd_id = next_by_cmd_id;
				} else {
					host->h_next_by_cmd_id = NULL;
				}

				g_hash_table_insert(db->hosts_by_host_name_hash_table, host->h_host_name, host);
				g_hash_table_insert(db->hosts_by_address_hash_table, host->h_address, host);
				g_hash_table_insert(db->hosts_by_cmd_id_hash_table, &host->h_cmd_id, host);

				DEBUG("fetched host: %s [ip: %s] [cmd_id: %d] [cmdexp: %s]", host->h_host_name, host->h_address, cmd_id, host->h_expanded_text);

				count++;

			} else {
				log_critical(0, "error in fetch_hosts(): cannot expand %s", command->c_expanded_text);
			}
		}
	}

	DEBUG("=== fetched %d host(s) ===", count);

	free_result(result);

	return count;
}


/*
 * fetch services
 */

static int fetch_svcs(void)
{
	MYSQL_RES *result;
	MYSQL_ROW row;
	int cmd_id;
	struct command_t *command;
	struct svc_t *svc, *next_by_host_name, *next_by_host_address, *next_by_cmd_id;
	int count = 0;
	char *expanded_text;

	query("Q_FETCH_SVCS");
	result = store_result();

	db->svcs_by_host_name_hash_table = g_hash_table_new(g_str_hash, g_str_equal);
	db->svcs_by_host_address_hash_table = g_hash_table_new(g_str_hash, g_str_equal);
	db->svcs_by_cmd_id_hash_table = g_hash_table_new(g_int_hash, g_int_equal);

	while ((row = fetch_row(result))) {
		cmd_id = atoi(row[3]);
		command = (struct command_t *) g_hash_table_lookup(db->commands_hash_table, &cmd_id);

		if (command != NULL) {
			svc = xmalloc(sizeof *svc);
			svc->svc_cmd_id = cmd_id;
			svc->svc_description = xstrdup(row[0]);
			svc->svc_host_name = xstrdup(row[1]);
			svc->svc_host_address = xstrdup(row[2]);

			svc->svc_args_no = split_args(row[4], &svc->svc_args);
			
			expanded_text = command_expand_3(command->c_expanded_text, svc->svc_description, svc->svc_host_address, svc->svc_args, svc->svc_args_no);

			if (expanded_text != NULL) {
				svc->svc_expanded_text = expanded_text;

				if ((next_by_host_name = g_hash_table_lookup(db->svcs_by_host_name_hash_table, svc->svc_host_name)) != NULL) {
					g_hash_table_steal(db->svcs_by_host_name_hash_table, svc->svc_host_name);
					svc->svc_next_by_host_name = next_by_host_name;
				} else {
					svc->svc_next_by_host_name = NULL;
				}

				if ((next_by_host_address = g_hash_table_lookup(db->svcs_by_host_address_hash_table, svc->svc_host_address)) != NULL) {
					g_hash_table_steal(db->svcs_by_host_address_hash_table, svc->svc_host_address);
					svc->svc_next_by_host_address = next_by_host_address;
				} else {
					svc->svc_next_by_host_address = NULL;
				}

				if ((next_by_cmd_id = g_hash_table_lookup(db->svcs_by_cmd_id_hash_table, &cmd_id)) != NULL) {
					g_hash_table_steal(db->svcs_by_cmd_id_hash_table, &cmd_id);
					svc->svc_next_by_cmd_id = next_by_cmd_id;
				} else {
					svc->svc_next_by_cmd_id = NULL;
				}

				g_hash_table_insert(db->svcs_by_host_name_hash_table, svc->svc_host_name, svc);
				g_hash_table_insert(db->svcs_by_host_address_hash_table, svc->svc_host_address, svc);
				g_hash_table_insert(db->svcs_by_cmd_id_hash_table, &svc->svc_cmd_id, svc);

				DEBUG("fetched service: %s [host: %s] [ip: %s] [cmd_id: %d] [cmdexp: %s]", svc->svc_description, svc->svc_host_name, svc->svc_host_address, cmd_id, svc->svc_expanded_text);

				count++;
			} else {
				log_critical(0, "error in fetch_svcs(): cannot expand %s", command->c_expanded_text);
			}

		}
	}

	DEBUG("=== fetched %d service(s) ===", count);

	free_result(result);

	return count;
}


/*
 * fetch trap handler
 */

static int fetch_trap_handlers(void)
{
	MYSQL_RES *result;
	MYSQL_ROW row;
	int cmd_id;
	struct command_t *command;
	struct trap_handler_t *trap_handler;
	int count = 0;

	query("Q_FETCH_TRAP_HANDLERS");

	result = store_result();

	db->trap_handlers_hash_table = g_hash_table_new(g_str_hash, g_str_equal);

	while ((row = fetch_row(result))) {
		cmd_id = atoi(row[1]);
		command = (struct command_t *) g_hash_table_lookup(db->commands_hash_table, &cmd_id);

		if (command != NULL) {
			trap_handler = xmalloc(sizeof *trap_handler);
			trap_handler->th_oid = xstrdup(row[0]);
			trap_handler->th_trap_command = command;
			g_hash_table_insert(db->trap_handlers_hash_table, trap_handler->th_oid, trap_handler);

			DEBUG("fetched trap handler: [oid: %s] [cmd_id: %d]", trap_handler->th_oid, cmd_id);

			count++;
		}
	}

	DEBUG("=== fetched %d trap handler(s) ===", count);

	free_result(result);

	return count;
}



/*
 *     Class constructor
 *
 ******************************************************************************/

/*
 * initialize db subsystem
 */

void db_init(int is_daemon)
{
	int count_resources, count_commands, count_hosts, count_svcs, count_trap_handlers;

	db = xmalloc(sizeof *db);

	if ((db->conn = mysql_init(NULL)) == NULL)
		log_critical(0, "memory insufficient to instantiate db connection");

		DEBUG("db_host: %s", config_get_option_value(":db_host"));
		DEBUG("db_user: %s", config_get_option_value(":db_user"));
		/*DEBUG("db_password: %s", config_get_option_value(":db_password"));*/
		DEBUG("db_name: %s", config_get_option_value(":db_name"));
	if ((db->conn = mysql_real_connect(db->conn,
		config_get_option_value(":db_host"),
		config_get_option_value(":db_user"),
		config_get_option_value(":db_password"),
		config_get_option_value(":db_name"), 0, NULL, 0)) == NULL)
	{
		log_critical(0, "cannot connect to db: %s", mysql_error(db->conn));
	} 

	count_resources = fetch_resources();

	if (is_daemon) {
		count_commands = fetch_commands();
		if (!count_commands)
			DEBUG("no commands found");

		count_hosts = fetch_hosts();
		count_svcs = fetch_svcs();

		if (!(count_hosts || count_svcs))
			DEBUG("no hosts and no services found");

		count_trap_handlers = fetch_trap_handlers();
		if (!count_trap_handlers)
			DEBUG("no trap handlers found");
	}

	mysql_close(db->conn);
}



/*
 *     Lookup functions
 *
 ******************************************************************************/


char *db_lookup_resource(const char *key)
{
	struct resource_t *resource;
	char *value;
	char *buffer;
	
	if ((resource = (struct resource_t *) g_hash_table_lookup(db->resources_hash_table, key)) != NULL && (value = resource->r_value) != NULL) {
		buffer = xstrdup(value);
		return buffer;
	} else {
		return NULL;
	}
}


int db_lookup_cmd_id_by_oid(const char *oid)
{
	struct trap_handler_t *trap_handler;
	struct command_t *command;

	if (oid == NULL)
		return -1;

	trap_handler = (struct trap_handler_t *) g_hash_table_lookup(db->trap_handlers_hash_table, oid);
	if (trap_handler == NULL)
		return -1;
	
	command = trap_handler->th_trap_command;
	if (command == NULL)
		return -1;

	return command->c_id;
}


struct command_t *db_lookup_command_by_cmd_id(int cmd_id)
{
	if (cmd_id == -1)
		return NULL;
	
	return (struct command_t *) g_hash_table_lookup(db->commands_hash_table, &cmd_id);
}


char *db_lookup_filename_by_cmd_id(int cmd_id)
{
	struct command_t *command;

	if ((command = db_lookup_command_by_cmd_id(cmd_id)) == NULL)
		return NULL;

	return command->c_filename;
}


int db_lookup_use_sender_address_by_cmd_id(int cmd_id, int *use_sender_address)
{
	struct command_t *command;

	if ((command = db_lookup_command_by_cmd_id(cmd_id)) == NULL)
		return 0;

	*use_sender_address = command->c_use_sender_address;

	return 1;
}


char *db_lookup_expanded_text_by_cmd_id(int cmd_id)
{
	struct command_t *command;

	if (cmd_id == -1)
		return NULL;
	
	if ((command = db_lookup_command_by_cmd_id(cmd_id)) == NULL)
		return NULL;

	return command->c_expanded_text;
}


struct host_t *db_lookup_host_by_cmd_id(int cmd_id, struct host_t *host, int flags)
{
	if (host != NULL) {
		if (flags & DB_LOOKUP_NEXT_BY_HOST_NAME)
			return host->h_next_by_name;
		else if (flags & DB_LOOKUP_NEXT_BY_HOST_ADDRESS)
			return host->h_next_by_address;
		else
			return NULL;
	} else {
		return (struct host_t *) g_hash_table_lookup(db->hosts_by_cmd_id_hash_table, &cmd_id);
	}
}


struct svc_t *db_lookup_svc_by_cmd_id(int cmd_id, struct svc_t *svc, int flags)
{
	if (svc != NULL) {
		if (flags & DB_LOOKUP_NEXT_BY_HOST_NAME)
			return svc->svc_next_by_host_name;
		else if (flags & DB_LOOKUP_NEXT_BY_HOST_ADDRESS)
			return svc->svc_next_by_host_address;
		else
			return NULL;
		}
	else
		return (struct svc_t *) g_hash_table_lookup(db->svcs_by_cmd_id_hash_table, &cmd_id);
}


struct host_t *db_lookup_host_by_host_name(const char *host_name)
{
	if (is_empty(host_name))
		return NULL;
	else
		return (struct host_t *) g_hash_table_lookup(db->hosts_by_host_name_hash_table, host_name);
}


struct host_t *db_lookup_host_by_address(const char *address)
{
	if (is_empty(address))
		return NULL;
	else
		return (struct host_t *) g_hash_table_lookup(db->hosts_by_address_hash_table, address);
}


struct svc_t *db_lookup_svc_by_host_name(const char *host_name, struct svc_t *svc, int flags)
{
	if (is_empty(host_name))
		return NULL;

	if (svc != NULL) {
		if (flags & DB_LOOKUP_NEXT_BY_HOST_NAME)
			return svc->svc_next_by_host_name;
		else if (flags & DB_LOOKUP_NEXT_BY_HOST_ADDRESS)
			return svc->svc_next_by_host_address;
		else
			return NULL;
		}
	else
		return (struct svc_t *) g_hash_table_lookup(db->svcs_by_host_name_hash_table, host_name);
}


struct svc_t *db_lookup_svc_by_host_address(const char *host_address, struct svc_t *svc, int flags)
{
	if (is_empty(host_address))
		return NULL;

	if (svc != NULL) {
		if (flags & DB_LOOKUP_NEXT_BY_HOST_NAME)
			return svc->svc_next_by_host_name;
		else if (flags & DB_LOOKUP_NEXT_BY_HOST_ADDRESS)
			return svc->svc_next_by_host_address;
		else
			return NULL;
		}
	else
		return (struct svc_t *) g_hash_table_lookup(db->svcs_by_host_address_hash_table, host_address);
}



/*
 *     Extraction functions
 *
 ******************************************************************************/


char *db_extract_host_address(struct host_t *host, struct svc_t *svc)
{
	if (host != NULL)
		return host->h_address;
	else if (svc != NULL)
		return svc->svc_host_address;
	else
		return NULL;
}


char *db_extract_host_name(struct host_t *host, struct svc_t *svc)
{
	if (host != NULL)
		return host->h_host_name;
	else if (svc != NULL)
		return svc->svc_host_name;
	else
		return NULL;
}


char *db_extract_svc_desc(struct svc_t *svc)
{
	if (svc != NULL)
		return svc->svc_description;
	else
		return NULL;
}


char *db_extract_expanded_text(struct host_t *host, struct svc_t *svc)
{
	if (host != NULL)
		return host->h_expanded_text;
	else if (svc != NULL)
		return svc->svc_expanded_text;
	else
		return NULL;
}
