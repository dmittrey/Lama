#ifndef OPSTACK_H
#define OPSTACK_H

#include <stdint.h>
#include <stddef.h>

#define OPSTACK_INITIAL_SIZE 256
#define OPSTACK_MAX_SIZE     65536

typedef struct {
    int32_t *operands;      /* stack data */
    size_t size;        /* current number of elements */
    size_t capacity;    /* allocated capacity */
} opstack_t;

opstack_t* create_opstack();

void destroy_opstack(opstack_t*);

void opstack_push(opstack_t*, int32_t value);
int32_t opstack_pop(opstack_t*);

#endif