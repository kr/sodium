/* mem.h - lisp-style data structure support */

#ifndef mem_h
#define mem_h

#include "gen.h"
#include "str.h"
#include "bytes.h"

typedef void(*na_fn_free)(datum);

typedef struct pair {
    datum car, cdr;
} *pair;

typedef struct method_item {
    datum name;
    datum addr;
} *method_item;

typedef struct method_table {
    datum size;
    struct method_item items[];
} *method_table;

datum cons(datum x, datum y);
datum make_array(uint len);
datum make_bytes(uint len);
datum make_closure(datum env, uint *table);

/* Note: *x must be the only pointer to x */
void install_fz(datum *, na_fn_free);

size_t datum_size(datum d);


datum make_opaque(size_t size, datum mtab);


datum array_get(datum arr, uint index);
datum array_put(datum arr, uint index, datum val);
uint array_len(datum arr);

char *bytes_contents(datum bytes);

inline pair datum2pair(datum d);

#define car(x) (datum2pair(x)->car)
#define cdr(x) (datum2pair(x)->cdr)

#define caar(x) (car(car(x)))
#define cadr(x) (car(cdr(x)))
#define cdar(x) (cdr(car(x)))
#define cddr(x) (cdr(cdr(x)))

#define caddr(x) (car(cddr(x)))

#define cdaddr(x) (cdr(caddr(x)))

#define HEAP_SIZE (2 * 1024 * 1024)

extern size_t *busy_chunks, *old_chunks;

void init_mem(void);

/*bool*/
int pairp(datum x);
int arrayp(datum x);
int bytesp(datum x);
int broken_heartp(datum x);

/*bool*/

void become(datum *a, datum *b, int keep_b);

#endif /*mem_h*/
