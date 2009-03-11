/* mem.h - lisp-style data structure support */

#ifndef mem_h
#define mem_h

#include "gen.h"

/* These are used commonly, so they're here for convenience. */
#include "str.h"
#include "pair.h"

typedef void(*na_fn_free)(datum);

typedef struct method_item {
    datum name;
    datum addr;
} *method_item;

typedef struct method_table {
    datum size;
    struct method_item items[];
} *method_table;

/* Note: *x must be the only pointer to x */
void install_fz(datum *x, na_fn_free);

size_t datum_size(datum d);
datum datum_mtab(datum d);


datum make_opaque(size_t size, datum mtab);
datum make_record(size_t len, datum mtab, datum a, datum b);

datum make_opaque_permanent(size_t size, datum mtab);
datum make_record_permanent(size_t len, datum mtab, datum a, datum b);

void init_mem(void);

/*bool*/
int broken_heartp(datum x);

/*bool*/

void become(datum *a, datum *b, int keep_b);

#endif /*mem_h*/
