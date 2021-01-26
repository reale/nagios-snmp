/*
 *     N a g i o s   S N M P   T r a p   D a e m o n 
 *
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/


/*
 *     util.c --- utility functions
 *
 ******************************************************************************
 ******************************************************************************/



#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/syscall.h>

#include "nagiostrapd.h"



/*
 *     Malloc & al.
 *
 ******************************************************************************/


void *xmalloc(size_t size)
{
	void *ptr = malloc(size);

	if (ptr == NULL)
		log_critical(0, "insufficient memory");

	return ptr;
}


void *xcalloc(size_t nmemb, size_t size)
{
	void *ptr = calloc(nmemb, size);

	if (ptr == NULL)
		log_critical(0, "insufficient memory");

	return ptr;
}


void *xrealloc(void *ptr, size_t size)
{
	void *ptr2 = realloc(ptr, size);

	if (ptr2 == NULL)
		log_critical(0, "insufficient memory");

	return ptr2;
}



/*
 *     Strings...
 *
 ******************************************************************************/


char *xstrdup(const char *s)
{
	char *t;
	
	if (is_empty(s))
		return NULL;

	t = strdup(s);

	if (t == NULL)
		log_critical(0, "insufficient memory");

	return t;
}


size_t xstrlen(const char *s)
{
	if (is_empty(s))
		return 0;
	else
		return strlen(s);
}


/*
 * remove from STR all character present in REM
 */

char *remove_chars(const char *str, const char *rem)
{
	char *buffer;
	int i;

	if (str == NULL)
		return NULL;

	buffer = xmalloc(strlen(str) + 1);
	
	for (i = 0; *str != '\0'; str++) {
		const char *p = rem;
		int found = 0;

		for (; *p != '\0'; p++) {
			if (*str == *p) found = 1;
		}

		if (found == 0) {
			buffer[i] = *str;
			i++;
		}
	}

	buffer[i] = '\0';

	if (strlen(buffer) == 0) {
		free(buffer);
		return NULL;
	}

	return buffer;
}


/*
 * remove from STR duplicate tabs and spaces
 */

char *remove_spaces(const char *str)
{
	const char spaces[] = " \t";
	char *buffer;
	int i;

	if (str == NULL)
		return NULL;

	buffer = malloc(strlen(str) + 1);
	
	for (i = 0; *str != '\0'; str++) {
		const char *p = spaces;
		int found = 0;

		for (; *p != '\0'; p++) {
			if (*str == *p) found = 1;
		}

		if (found == 0)
			buffer[i] = *str;
		else
			buffer[i] = ' ';

		i++;
	}

	buffer[i] = '\0';

	if (strlen(buffer) == 0) {
		free(buffer);
		return NULL;
	}

	return buffer;
}


/*
 * is STR an integer?
 */

int is_integer(const char *str)
{
	char c;

	if (str == NULL)
		return 0;

	while ((c = *str++) != '\0') {
		if (!isdigit(c))
			return 0;
	}

	return 1;
}


/*
 * is S a NULL or empty string?
 */
int is_empty(const char *s)
{
	return (s == NULL || strlen(s) == 0);
}



/*
 *     Quoting
 *
 ******************************************************************************/


/*
 * quote string
 */
char *quote(const char *string)
{
	register unsigned char c;
	char *t, *result;
	const char *s;

	result = xmalloc(xstrlen(string)*2 + 3);
	t = result;

	*t++ = '"';

	for (s = string; s && (c = *s); s++) {
		if (c == '"')
			*t++ = '\\';

		*t++ = c;
	}

	*t++ = '"';
	*t = '\0';

	return result;
}


/*
 * remove leading and trailing double quotes
 */
char *remove_quotes(const char *string)
{
	register unsigned char c;
	char *t, *result;
	const char *s;
	int i, len;

	if (string == NULL)
		return NULL;

	len = strlen(string);
	result = xmalloc(len + 1);
	t = result;

	for (i = 0, s = string; s && (c = *s); i++, s++) {
		if (c != '"' || (i != 0 && i != len-1))
			*t++ = *s;
	}

	*t = '\0';

	return result;
}


/*
 * Quote special characters in STRING using backslashes.  Return a new
 * string.
 */
char *backslash_quote(char *string)
{
  int c;
  char *result, *r, *s;

  if (string == NULL)
  	return NULL;

  result = xmalloc (2 * strlen (string) + 1);

  for (r = result, s = string; s && (c = *s); s++)
    {
      switch (c)
	{
#if 0
	case ' ': case '\t': case '\n':		/* IFS white space */
#endif
	case '\'': case '"': case '\\':		/* quoting chars */
#if 0
	case '|': case '&': case ';':		/* shell metacharacters */
	case '(': case ')': case '<': case '>':
	case '!': case '{': case '}':		/* reserved words */
	case '*': case '[': case '?': case ']':	/* globbing chars */
	case '^':
#endif
	case '$': case '`':			/* expansion chars */
#if 0
	case ',':				/* brace expansion */
#endif
	  *r++ = '\\';
	  *r++ = c;
	  break;
#if 0
	case '~':				/* tilde expansion */
	  if (s == string || s[-1] == '=' || s[-1] == ':')
	    *r++ = '\\';
	  *r++ = c;
	  break;

	case CTLESC: case CTLNUL:		/* internal quoting characters */
	  *r++ = CTLESC;			/* could be '\\'? */
	  *r++ = c;
	  break;
#endif

	case '#':				/* comment char */
	  if (s == string)
	    *r++ = '\\';
	  /* FALLTHROUGH */
	default:
	  *r++ = c;
	  break;
	}
    }

  *r = '\0';
  return (result);
}



/*
 *     File locking
 *
 ******************************************************************************/


void get_file_lock(int fd)
{
	struct flock fl;

	fl.l_type   = F_WRLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start  = 0;
	fl.l_len    = 0;
	fl.l_pid    = getpid();

	if (fcntl(fd, F_SETLKW, &fl) < 0)
		log_critical(errno, "cannot acquire lock on fd %d", fd);
}


void release_file_lock(int fd)
{
	struct flock fl;

	fl.l_type   = F_UNLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start  = 0;
	fl.l_len    = 0;
	fl.l_pid    = getpid();

	if (fcntl(fd, F_SETLKW, &fl) < 0)
		log_critical(errno, "cannot relinquish lock on fd %d", fd);
}



/*
 *     Miscellaneous
 *
 ******************************************************************************/


size_t get_file_size(FILE *stream)
{
	/* WARNING: we don't bother to acquire a lock... */

	size_t numbytes;

	/* get file length */
	fseek(stream, 0L, SEEK_END);
	numbytes = ftell(stream);

	/* reposition the stream at the beginning... */
	rewind(stream);

	return numbytes;
}


/*
 * extract filename from command
 */

char *extract_filename(const char *command)
{
	char c;
	const char *ptr;
	char *filename;
	int i = 0;

	if (command == NULL || strlen(command) == 0)
		return NULL;

	ptr = command;

	while ((c = *ptr++) != '\0') {
		if (c == ' ' || c == '\t' || c == '\n') {
			i--;
			break;
		}
		i++;
	}
	
	if (i <= 0)
		return NULL;

	filename = xmalloc(i+2);
	memcpy(filename, command, i+1);
	filename[i+1] = '\0';

	return filename;
}


/*
 * set proper execute permissions
 */

#define PERM_MASK (S_IXUSR | S_IXGRP | S_IXOTH)

int set_permissions(const char *filename)
{
	struct stat st;

	if (stat(filename, &st) == -1) {
		log_error(errno, "cannot stat %s", filename);
		return 0;
	}

	if ((st.st_mode & PERM_MASK) == 0) {
		/* set execute permissions */
		
		if (chmod(filename, st.st_mode | PERM_MASK) == -1) {
			log_error(errno, "cannot set exec permissions on %s", filename);
			return 0;
		}
	}

	return 1;
}


/*
 * get bogomips
 */

float get_bogomips(void)
{
	FILE *pipe;
	float bogomips = 0;
	char buffer[128];

	if ((pipe = popen("cat /proc/cpuinfo | tr -d ' ' | awk -F: '/bogomips/ {print $2}'", "r")) != NULL) {
		int matched;
		matched = fscanf(pipe, "%s\n", buffer);
		if (matched == 1)
			bogomips = atof(buffer);


		pclose(pipe);
	}

	if (bogomips < 1000)
		bogomips = 1000;

	return bogomips;
}


pid_t gettid(void)
{
	pid_t tid;

	tid = syscall(SYS_gettid);
	return tid;
}

