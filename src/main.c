/*
 *     N a g i o s   S N M P   T r a p   D a e m o n 
 *
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/


/*
 *     main.c --- entry point
 *
 ******************************************************************************
 ******************************************************************************/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>

#include "nagiostrapd.h"



/*
 *     Global declarations
 *
 ******************************************************************************/


/* number of ignored arguments */
static int arguments_ignored = 0;

/* path of config file */
static char *config_file = NULL;

/* run as a daemon */
static int is_daemon = 1;

/* name of the command (plugin) to execute when in standalone mode */
static char *command = NULL;

/* OID to accept when in standalone mode */
static char *oid = NULL;

/* trap tokenizer string */
static char *trap_tokenizer = NULL;

/* diagnostics mode */
static int print_diagnostics = 0;

/* dump config mode */
static int dump_config = 0;

/* be verbose */
static int verbose = 1;

/* print help */
static int print_help = 0;

/* print version */
static int print_version = 0;

/* run as user... */
static char *username = NULL;

/* run as group... */
static char *groupname = NULL;



/*
 *     Main entry point
 *
 ******************************************************************************/


int main(int argc, char **argv)
{
	uid_t uid = 0;
	gid_t gid = 0;
	int c;

	/*
	 * parse command-line options
	 */

	while (1)
	{
		static struct option long_options[] =
		{
			{ "config-file", required_argument, 0, 'C' },

			{ "daemon", no_argument, &is_daemon, 1 },
			{ "standalone", no_argument, &is_daemon, 0 },

			{ "command", required_argument, 0, 'c' },
			{ "oid", required_argument, 0, 'O' },

			{ "user", required_argument, 0, 'u' },
			{ "group", required_argument, 0, 'g' },

			{ "trap-tokenizer", required_argument, 0, 'T' },

			{ "quiet", no_argument, &verbose, 0 },
			{ "verbose", no_argument, &verbose, 1 },

			{ "diagnostics", no_argument, &print_diagnostics, 1},

			{ "dump-config", no_argument, &dump_config, 1 },

			{ "help", no_argument, &print_help, 1 },
			{ "version", no_argument, &print_version, 1 },
			{0, 0, 0, 0}
		};

		/* getopt_long stores the option index here */
		int option_index = 0;

		c = getopt_long(argc, argv, "C:c:dDg:O:qST:u:v", long_options, &option_index);

		/* detect the end of the options */
		if (c == -1)
			break;

		switch (c) {
			case 0:
				break;
			case 'C':
				config_file = optarg;
				break;
			case 'c':
				command = optarg;
				break;
			case 'd':
				print_diagnostics = 1;
				break;
			case 'D':
				is_daemon = 1;
				break;
			case 'g':
				groupname = optarg;
				break;
			case 'O':
				oid = optarg;
				break;
			case 'q':
				verbose = 0;
				break;
			case 'S':
				is_daemon = 0;
				break;
			case 'T':
				trap_tokenizer = optarg;
				break;
			case 'u':
				username = optarg;
				break;
			case 'v':
				verbose = 1;
				break;
			case '?':
				/* getopt_long already printed an error message. */
				break;
			default:
				abort();
		}
	}

	/* are there any trailing args? */
	if (optind < argc)
		arguments_ignored = 1;

	/* should we only print version? */
	if (print_version) {
		interface_print_version();
		exit(EXIT_SUCCESS);
	}

	/* should we print usage instead? */
	if (print_help) {
		interface_print_help();
		exit(EXIT_SUCCESS);
	}

	if (print_diagnostics || dump_config)
		verbose = 0;

	/* print welcome msg */
	if (verbose)
		interface_print_welcome(is_daemon);

	/* check option sanity */
	if (arguments_ignored)
		fprintf(stderr, "WARNING: unrecognized arguments: ignored\n");
	if (!is_daemon && is_empty(command)) {
		fprintf(stderr, "ERROR: when in standalone mode (-S) you must specify command (-c <CMD>)\n");
		exit(EXIT_FAILURE);
	}
	if (!is_daemon && is_empty(oid)) {
		fprintf(stderr, "ERROR: when in standalone mode (-S) you must specify OID (-O <OID>)\n");
		exit(EXIT_FAILURE);
	}

	if (!is_empty(username)) {
		if (!is_daemon) {
			fprintf(stderr, "WARNING: -u (--username) switch ignored when in standalone mode\n");
		} else {
			struct passwd *pwent = getpwnam(username);
			if (pwent != NULL)
				uid = pwent->pw_uid;
			else {
				fprintf(stderr, "ERROR: user %s not found\n", username);
				exit(EXIT_FAILURE);
			}
		}
	}
	if (!is_empty(groupname)) {
		if (!is_daemon) {
			fprintf(stderr, "WARNING: -g (--groupname) switch ignored when in standalone mode\n");
		} else {
			struct group *grent = getgrnam(groupname);
			if (grent != NULL)
				gid = grent->gr_gid;
			else {
				fprintf(stderr, "ERROR: group %s not found\n", groupname);
				exit(EXIT_FAILURE);
			}
		}
	}

	/* load config */
	config_load(config_file, trap_tokenizer);

	/* should we just dump config? */
	if (dump_config) {
		config_dump();
		exit(EXIT_SUCCESS);
	}

	/* init logging facility */
	log_init(is_daemon && !print_diagnostics, verbose);

	if (print_diagnostics) {
		diagnostics2_do();
		exit(EXIT_SUCCESS);
	}

	DEBUG("*** starting... ***");

#ifndef NDEBUG
	interface_print_debug();
#endif

	/* open channel */
	channel_init(is_daemon);

	/* read queries */
	query_init();

	/* fetch data from db */
	db_init(is_daemon);

	/* init trap parsing machine */
	trap_init();

	/* init trap logging subsystem */
	traplog_init();

	/* init exec subsystem */
	exec_init();


	/*
	 * MAIN LOOP
	 */

	if (is_daemon) {
		daemon_init(uid, gid);
	} else {
		standalone_init(oid, command);
		standalone_start_main_loop(oid, command);
	}

	/* exit */
	exit(EXIT_SUCCESS);
}
