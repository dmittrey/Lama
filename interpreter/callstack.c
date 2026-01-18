#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "callstack.h"

callstack_t* create_callstack()
{
    callstack_t *stack = malloc(sizeof(callstack_t));
    if (!stack) {
        return NULL;
    }

    stack->frames = malloc(CALLSTACK_INITIAL_SIZE * sizeof(call_frame_t));
    if (!stack->frames) {
        free(stack);
        return NULL;
    }

    stack->size = 0;
    stack->capacity = CALLSTACK_INITIAL_SIZE;

    return stack;
}

void destroy_callstack(callstack_t* stack)
{
    if (stack) {
        free(stack->frames);
        free(stack);
    }
}