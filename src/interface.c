/*
 *     N a g i o s   S N M P   T r a p   D a e m o n 
 *
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/


/*
 *     interface.c --- command line interface
 *
 ******************************************************************************
 ******************************************************************************/



#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "nagiostrapd.h"
#include "version.h"



/*
 *     Private methods
 *
 ******************************************************************************/


static char *get_revision(void)
{
	char *rev_orig = PROGREVISION;
	char *p, *start, *rev;
	char c;
	int found;
	int len;
	
	p = rev_orig;

	found = 0;
	while ((c = *++p) != '\0') {
		if (c == ' ' || c == '\t') {
			found = 1;
			break;
		}
	}

	if (!found)
		return NULL;
	
	found = 0;
	while ((c = *++p) != '\0') {
		if (c != ' ' && c != '\t') {
			found = 1;
			break;
		}
	}

	if (!found)
		return NULL;

	start = p;

	found = 0;
	len = 0;
	while ((c = *++p) != '\0') {
		len++;
		if (c == ' ' || c == '\t') {
			found = 1;
			break;
		}
	}

	if (!found)
		return NULL;

	rev = xmalloc(len+1);
	memcpy(rev, start, len);
	rev[len] = '\0';

	return rev;

}


static void print_key(void)
{
	char *key = key_get();

	fprintf(stderr, "Key %s\n", key);
	free(key);
}


static void print_version(void)
{
	char *revision = get_revision();

	fprintf(stderr, "Version %s (rev. %s)\n", PROGVERSION, revision);
	free(revision);
}


static void print_version_all(void)
{
	print_version();
	print_key();
}



/*
 *     Public methods
 *
 ******************************************************************************/


#ifndef NDEBUG
# define DEBUG_STRING " (DEBUG)"
#else
# define DEBUG_STRING ""
#endif

void interface_print_welcome(int is_daemon)
{
	if (is_daemon) {
		fprintf(stderr, "Starting %s%s\n", PROGNAME_FULL, DEBUG_STRING);
	} else {
		fprintf(stderr, "%s (standalone mode)%s\n", PROGNAME_FULL, DEBUG_STRING);
	}

	print_version_all();
}


void interface_print_version(void)
{
	fprintf(stderr, "%s\n", PROGNAME_FULL);

	print_version_all();
}


void interface_print_help(void)
{
	interface_print_version();

	fputs("\n\
Possible options are:\n\n\
  -C, --config-file           path to config file\n\n\
      --dump-config           dump config\n\n\
  -D, --daemon                run as daemon\n\n\
  -u, --username              run as user\n\n\
  -g, --groupname             run as group\n\n\
  -S, --standalone            run as standalone\n\
  -c, --command               specify command\n\
  -O, --oid                   specify OID\n\n\
  -d, --diagnostics           print diagnostics\n\n\
  -T, --trap-tokenizer        trap tokenizer string\n\n\
  -q, --quiet                 run silently\n\
  -v, --verbose               be verbose\n\n\
      --help                  print help\n\
      --version               print version\n\
", stdout);
}


#ifndef NDEBUG
void interface_print_debug(void)
{
	char *revision = get_revision();
	char *key = key_get();

	DEBUG("*** Version %s (rev. %s) ***", PROGVERSION, revision);
	DEBUG("*** Key %s ***", key);

	free(revision);
	free(key);
}
#endif
