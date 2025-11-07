#pragma once
#include "utils.h"
#include <sel4/simple_types.h>
#include <cspace/cspace.h>

ut_t *create_cap(seL4_CPtr *cap, seL4_ObjectType type, int bits);
void free_cap(ut_t *ut, seL4_CPtr cap);

