/* lxc.h - lxc LX-to-C modules */

#ifndef LXC_H
#define LXC_H

#include <stdlib.h>

typedef struct lxc_module {
    const char *name;

    uint *instrs;
    uint instrs_count;

    uint *str_offsets;
    uint *ime_offsets;
    uint *sym_offsets;
} *lxc_module;

#endif /*LXC_H*/
