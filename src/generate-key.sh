#!/bin/bash

#
#     N a g i o s   S N M P   T r a p   D a e m o n 
#
##############################################################################
##############################################################################
##############################################################################

#
#     Generate a casual string and write a C file
#
##############################################################################
##############################################################################



keylen=32

function genkey()
{
	length=$1

	cat /dev/urandom | tr -cd '[:alnum:]' | dd bs=$length count=1 2>/dev/null
}

cat <<???
#include "nagiostrapd.h"
#define _KEY "`genkey $keylen`"

char *key_get(void)
{
	return xstrdup(_KEY);
}
???
