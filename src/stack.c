/*
 *     N a g i o s   S N M P   T r a p   D a e m o n 
 *
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/


/*
 *     stack.c --- a stack implementation
 *
 ******************************************************************************
 ******************************************************************************/



#include <stdlib.h>
#include <assert.h>

#include "nagiostrapd.h"



struct stack_item_t {
	void *data;
	struct stack_item_t *next;
};

struct stack_t {
	struct stack_item_t *top;
	struct stack_item_t *bottom;
	int depth;
	stack_data_destructor_t destructor;
	int executions;
};


struct stack_t *stack_init(stack_data_destructor_t destructor)
{
	struct stack_t *stack;

	stack = xmalloc(sizeof *stack);

	stack->top = NULL;
	stack->bottom = NULL;
	stack->depth = 0;
	stack->destructor = destructor;
	stack->executions = 0;

	return stack;
}


void stack_increment_executions(struct stack_t *stack)
{
	stack->executions++;
}


int stack_count_executions(struct stack_t *stack)
{
	return stack->executions;
}


void stack_free(struct stack_t *stack)
{
	struct stack_item_t *top, *next;

	if (stack == NULL) {
		DEBUG("attempted to free NULL stack");
		return;
	}

	top = stack->top;

	if (top == NULL)
		return;

	while (top != NULL) {
		next = top->next;
		stack->destructor(top->data);
		free(top);
		top = next;
	}

	free(stack);

	DEBUG("stack freed");
}


int stack_get_depth(struct stack_t *stack)
{
	if (stack == NULL) {
		DEBUG("attempted to get depth of a NULL stack");
		return -1;
	}

	return stack->depth;
}


int stack_push(struct stack_t *stack, void *data)
{
	struct stack_item_t *new;

	if (stack == NULL) {
		DEBUG("attempted to push onto a NULL stack");
		return 0;
	}

	assert(stack_get_depth(stack) >= 0);

	new = xmalloc(sizeof *new);
	new->data = data;

	new->next = stack->top;
	stack->top = new;

	stack->depth++;

	return 1;
}


struct stack_item_t *stack_pop(struct stack_t *stack)
{
	struct stack_item_t *item, *top;

	if (stack == NULL || stack_get_depth(stack) == 0) {
		DEBUG("attempted to pop from a NULL or empty stack");
		return NULL;
	}

	item = stack->top;
	top = stack->top->next;
	item->next = NULL;
	stack->top = top;

	stack->depth--;
	
	return item;
}


void *stack_get_data(struct stack_item_t *item)
{
	if (item == NULL)
		return NULL;
	else
		return item->data;
}
