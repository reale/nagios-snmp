/*
 *     N a g i o s   S N M P   T r a p   D a e m o n 
 *
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/


/*
 *     startup.c --- startup is completed or not?
 *
 ******************************************************************************
 ******************************************************************************/


#include "nagiostrapd.h"

static int startup_completed = 0;

void startup_set_completed(int status)
{
	DEBUG("setting status: %d", status);
	startup_completed = status;
}

int startup_is_completed(void)
{
	return startup_completed;
}
