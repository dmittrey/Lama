#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "opstack.h"

opstack_t* create_opstack()
{
    opstack_t *stack = malloc(sizeof(opstack_t));
    if (!stack) {
        return NULL;
    }

    stack->operands = malloc(OPSTACK_INITIAL_SIZE * sizeof(int32_t));
    if (!stack->operands) {
        free(stack);
        return NULL;
    }

    stack->size = 0;
    stack->capacity = OPSTACK_INITIAL_SIZE;

    return stack;
}

void destroy_opstack(opstack_t* stack)
{
    if (stack) {
        free(stack->operands);
        free(stack);
    }
}

static int opstack_resize(opstack_t* stack, size_t new_capacity)
{
    if (new_capacity > OPSTACK_MAX_SIZE) {
        fprintf(stderr, "Stack overflow: requested capacity %zu exceeds maximum %d\n",
                new_capacity, OPSTACK_MAX_SIZE);
        return 0;
    }

    int32_t *new_operands = realloc(stack->operands, new_capacity * sizeof(int32_t));
    if (!new_operands) {
        fprintf(stderr, "Failed to resize operand stack\n");
        return 0;
    }

    stack->operands = new_operands;
    stack->capacity = new_capacity;
    return 1;
}

void opstack_push(opstack_t* stack, int32_t value)
{
    assert(stack != NULL);

    if (stack->size >= stack->capacity) {
        size_t new_capacity = stack->capacity * 2;
        if (!opstack_resize(stack, new_capacity)) {
            fprintf(stderr, "Stack push failed: cannot resize stack\n");
            exit(1);
        }
    }

    stack->operands[stack->size++] = value;
}

int32_t opstack_pop(opstack_t* stack)
{
    assert(stack != NULL);

    if (stack->size == 0) {
        fprintf(stderr, "Stack underflow: cannot pop from empty stack\n");
        exit(1);
    }

    return stack->operands[--stack->size];
}