/*
 *     N a g i o s   S N M P   T r a p   D a e m o n 
 *
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/


/*
 *     regex.c --- regex utils
 *
 ******************************************************************************
 ******************************************************************************/


#include <string.h>
#include <pcre.h>

#include "nagiostrapd.h"


pcre* regex_compile(const char *pattern)
{
	pcre* re;
	const char* error;
	int erroroffset;

	if (is_empty(pattern)) {
		DEBUG("called on empty pattern");
		return NULL;
	}

	DEBUG("called on pattern %s", pattern);

	re = pcre_compile(pattern, 0, &error, &erroroffset, NULL);

	if (!re) {
		log_critical(0,"PCRE compilation of pattern %s failed at expression offset %d: %s",
			pattern, erroroffset, error);
		return NULL;
	} else {
		DEBUG("pcre_compile() returned successfully");
		return re;
	}
}


int regex_execute(const pcre *re, const char *input, int *ovector, int ovecsize)
{
	int rc;

	if (re == NULL) {
		DEBUG("called on NULL regex");
		return -255;
	}

	if (is_empty(input)) {
		DEBUG("called on empty input");
		return -255;
	}

	rc = pcre_exec(re, NULL, input, strlen(input), 0, 0, ovector, ovecsize);

	if (rc < 0) {
		switch (rc) {
			case PCRE_ERROR_NOMATCH:
				break;

			default:
				DEBUG("pcre_exec(): match error: %d", rc);
				break;
		}
	}

	DEBUG("pcre_exec() returned status code %d", rc);
	return rc;
}
