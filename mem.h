/* mem.h - lisp-style data structure support */

#ifndef mem_h
#define mem_h

#include "gen.h"

/* These are used commonly, so they're here for convenience. */
#include "str.h"
#include "pair.h"

typedef struct method_item {
    datum name;
    datum addr;
} *method_item;

typedef struct method_table {
    datum size;
    struct method_item items[];
} *method_table;

/* Note: *x must be the only pointer to x */
void install_fz(datum *x);
datum first_reaper();

size_t datum_size(datum d);
datum datum_mtab(datum d);


datum make_opaque(size_t size, datum mtab);
datum make_record(size_t len, datum mtab, datum a, datum b);

datum make_opaque_permanent(size_t size, datum mtab);
datum make_record_permanent(size_t len, datum mtab, datum a, datum b);

int opaquep(datum x);

void init_mem(void);

/*bool*/
int broken_heartp(datum x);

/*bool*/

void become(datum *a, datum *b, int keep_b);

#define make_desc(format,len) ((size_t) (((len) << 4) | ((format) & 0xf)))

#define DATUM_FORMAT_RECORD 1
#define DATUM_FORMAT_BROKEN_HEART 3
#define DATUM_FORMAT_BACKPTR 5
#define DATUM_FORMAT_EMB_OPAQUE 7
#define DATUM_FORMAT_unused9 9
#define DATUM_FORMAT_unused11 11
#define DATUM_FORMAT_FZ 13
#define DATUM_FORMAT_OPAQUE 15

#endif /*mem_h*/
