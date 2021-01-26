/*
 *     N a g i o s   S N M P   T r a p   D a e m o n 
 *
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/


/*
 *     config.c --- interface to conf file
 *
 ******************************************************************************
 ******************************************************************************/



#define _BSD_SOURCE /* mkstemp() */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "nagiostrapd.h"
#include "iniparser.h"



/*
 *     Global declarations
 *
 ******************************************************************************/


struct option_element {
	char *key;
	char *value;
	int mandatory;
};

static struct option_element options[] = {
	{ ":daemonize", "true", 0 },
	{ ":db_host", NULL, 0 },
	{ ":db_user", NULL, 0 },
	{ ":db_password", NULL, 0 },
	{ ":db_name", NULL, 0 },
#ifndef NDEBUG
	{ ":debug_log", "/var/log/nagiostrapd.debug", 0 },
#endif
	{ ":diagnostics_pool", "/var/spool/nagiostrapd/diagnostics", 0 },
	{ ":enable_monitor", "true", 0 },
	{ ":error_log", "/var/log/nagiostrapd.error", 0 },
	{ ":listen_on_private_socket", "false", 0 },
	{ ":localhost_only", "true", 0 },
	{ ":local_socket_prefix", "/tmp/nagiostrapd", 0 },
	{ ":log_verbosity", "warning", 0 },
	{ ":monitor_port_number", "6111", 0 },
	{ ":nagios_pipe", "/usr/local/nagios/var/rw/nagios.cmd", 0 },
	{ ":pending_connections", "100", 0 },
	{ ":php_cli", "/usr/bin/php", 0 },
	{ ":pid_file", "/var/run/nagiostrapd.pid", 0 },
	{ ":plugins_max_executions", "10000", 0 },
	{ ":port_number", "6110", 0 },
	{ ":query_source_file", "/etc/nagiostrapd/nagiostrapd.sql", 0 },
	{ ":send_enabled", "true", 0 },
	{ ":nagios_conf_file", "/usr/local/nagios/nagios.conf.php", 0 },
	{ ":socket_reuse", "true", 0 },
	{ ":socket_set_nonblocking", "false", 0 },
	{ ":temp_dir", "/tmp", 0 },
	{ ":threadpool_size", "5", 0 },
	{ ":trap_log", "/var/log/nagiostrapd.traps", 0 },
	{ ":trap_log_delimiter", ",", 0 },
	{ ":trap_rate_threshold", ".005", 0 },
	{ ":trap_serialize_mode", "new", 0 },
	{ ":trap_tokenizer", "@TRAPTKN@", 0 },
	{ ":workers", "5", 0 },
	{ NULL, NULL, 0 }
};



/*
 *     Private methods
 *
 ******************************************************************************/


/*
 * set option value
 */

static int set_option_value(const char *key, const char *value, int not_null)
{
	int i;

	DEBUG("called");

	if (is_empty(key)) {
		DEBUG("key is empty");
		return 1;
	} else {
		DEBUG("key: %s", key);
	}

	if (is_empty(value)) {
		if (not_null) {
			DEBUG("attempting to set NULL value with NOT NULL required");
			return 0;
		}
	} else {
		DEBUG("value: %s", value);
	}

	for (i = 0 ;; i++) {
		if (options[i].key == NULL)
			break;

		if (!strcmp(options[i].key, key)) {
			if (options[i].mandatory && is_empty(value)) {
				DEBUG("attempting to set NULL value with mandatory option");
				return 0;
			}
			options[i].value = xstrdup(value);
			DEBUG("option %s set to %s", key, is_empty(value) ? "NULL" : value);
		}
	}

	DEBUG("done");

	return 1;
}


/*
 * parse conf (.ini) file
 */

static int parse_file(char *config_file)
{
	int i;
	char *value;
	dictionary *ini;

	DEBUG("called");

	if (config_file != NULL) {
		DEBUG("loading %s...", config_file);
		ini = iniparser_load(config_file);
	} else {
		DEBUG("loading %s...", CONFIG_FILE);
		ini = iniparser_load(CONFIG_FILE);
	}

	if (ini == NULL) {
		DEBUG("cannot parse config file: %s\n", config_file != NULL ? config_file : CONFIG_FILE);
		return 0;
	}

	for (i = 0 ;; i++) {
		if (options[i].key == NULL)
			break;

		DEBUG("reading option %s", options[i].key);
		value = iniparser_getstring(ini, options[i].key, options[i].value);
		if (is_empty(value)) {
			if (options[i].mandatory) {
				DEBUG("mandatory option %s not defined", options[i].key);
				return 0;
			} else {
				options[i].value = NULL;
				DEBUG("option %s set to NULL", options[i].key);
			}
		} else {
			options[i].value = xstrdup(value);
			DEBUG("option %s set to \"%s\"", options[i].key, value);
		}
	}

	iniparser_freedict(ini);

	DEBUG("done");

	return 1;
}


/*
 * extract parameters for connecting to the Nagios3 db backend
 * directly from the php configuration file
 */

static int parse_nagios_conf(const char *filename, const char *php_cli, const char *temp_dir)
{
	const char *template = TMPFILE_TEMPLATE;
	int auxfd;
	FILE *conffile, *auxfile, *pipe;
	char buffer[TMPBUFLEN], auxfilename[TMPBUFLEN_SMALL], command[TMPBUFLEN];
	char db_host[TMPBUFLEN], db_user[TMPBUFLEN], db_password[TMPBUFLEN], db_name[TMPBUFLEN];

	if ((conffile = fopen(filename, "r")) != NULL) {
		strncpy(auxfilename, temp_dir, strlen(temp_dir));
		strncpy(auxfilename+strlen(temp_dir), template, strlen(template));
		auxfilename[strlen(temp_dir)+strlen(template)] = '\0';

		if ((auxfd = mkstemp(auxfilename)) >= 0) {

			if ((auxfile = fdopen(auxfd, "w")) != NULL) { 
				while (fgets(buffer, sizeof(buffer), conffile) != NULL) {
					fputs(buffer, auxfile);
				}
				fclose(conffile);

				fprintf(auxfile,
					"<?php\n"
					"printf(\"%%s|%%s|%%s|%%s\\n\","
					"$conf_nagios['server'],"
					"$conf_nagios['user'],"
					"$conf_nagios['password'],"
					"$conf_nagios['db']);\n"
					"?>\n");
			} else {
				log_critical(errno, "cannot open Nagios3 config file descriptor");
				return 0;
			}
			
			fclose(auxfile);

			sprintf(command, "%s %s", php_cli, auxfilename);

			if ((pipe = popen(command, "r")) != NULL) {
				int matched;
				matched = fscanf(pipe, "%[^|]|%[^|]|%[^|]|%[^|\n]\n", db_host, db_user, db_password, db_name);
				if (matched != 4)
					log_critical(errno, "cannot parse Nagios3 config file");

				pclose(pipe);
			} else {
				log_critical(errno, "cannot open pipe to PHP");
				return 0;
			}

			unlink(auxfilename);
		} else {
			log_critical(errno, "cannot create temp file");
			return 0;
		}
	} else {
		log_critical(errno, "cannot open Nagios3 config file %s", filename);
		return 0;
	}

	if (!set_option_value(":db_host", db_host, 1)
		|| !set_option_value(":db_user", db_user, 1)
		|| !set_option_value(":db_password", db_password, 1)
		|| !set_option_value(":db_name", db_name, 1))
	{
		log_critical(0, "parameters for connecting to db not defined in Nagios3 config file");
		return 0;
	}

	return 1;
}



/*
 *     Public methods
 *
 ******************************************************************************/


/*
 * read value associated to KEY
 */

char *config_get_option_value(const char *key)
{
	int i;

	for (i = 0 ;; i++) {
		if (options[i].key == NULL)
			break;

		if (strcmp(options[i].key, key) == 0) {
			return options[i].value;
		}
	}

	log_critical(0, "option %s not defined", key);
	return NULL;
}


/*
 * dump config to standard output
 */

void config_dump(void)
{
	int i = 0;

	while (options[i].key) {
		printf("%s=\"%s\"\n", (options[i].key)+1, options[i].value);
		i++;
	}
}



/*
 *     Class constructor
 *
 ******************************************************************************/

void config_load(char *config_file, const char *trap_tokenizer)
{
	if (!parse_file(config_file))
		log_critical(0, "cannot load config file");

	if ((is_empty(config_get_option_value(":db_host"))
			|| is_empty(config_get_option_value(":db_user"))
			|| is_empty(config_get_option_value(":db_password"))
			|| is_empty(config_get_option_value(":db_name")))
		&& !parse_nagios_conf(config_get_option_value(":nagios_conf_file"),
			config_get_option_value(":php_cli"), config_get_option_value(":temp_dir")))
	{
		log_critical(0, "unable to find parameters for connecting to db");
	}

	if (!is_empty(trap_tokenizer)
		&& !set_option_value(":trap_tokenizer", trap_tokenizer, 1))
	{
		log_critical(0, "cannot set trap tokenizer");
	}
}
