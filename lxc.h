/* lxc.h - lxc LX-to-C modules */

#ifndef LXC_H
#define LXC_H

#include <stdlib.h>

typedef struct spair {
    uint car;
    uint cdr;
} *spair;

typedef struct static_datums_info {
    char *types;
    uint *entries;
} *static_datums_info;

typedef struct lxc_module {
    const char *name;

    const char **import_names;
    uint import_names_count;

    struct static_datums_info static_datums;
    uint static_datums_count;

    uint *instrs;
    uint instrs_count;

    uint *label_offsets;
} *lxc_module;

#endif /*LXC_H*/
