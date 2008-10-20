/* mem.h - lisp-style data structure support */

#ifndef mem_h
#define mem_h

#include "gen.h"
#include "str.h"
#include "bytes.h"

typedef void(*na_fn_free)(void *);

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

typedef struct closure {
    datum env;
    method_table table;
} *closure;

datum cons(datum x, datum y);
datum make_array(uint len);
datum make_bytes(uint len);
datum make_str(size_t size);
datum make_closure(datum env, uint *table);
datum grow_closure(datum *o, uint len, na_fn_free fn, void *data);
size_t datum_size(datum d);


datum array_get(datum arr, uint index);
datum array_put(datum arr, uint index, datum val);
uint array_len(datum arr);

char *bytes_contents(datum bytes);

inline pair datum2pair(datum d);

inline closure datum2closure(datum d);

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
#define in_busy_chunk_range(x) (((x) >= busy_chunks) && \
                               ((x) < &busy_chunks[HEAP_SIZE]))
#define in_old_chunk_range(x) (old_chunks && ((x) >= old_chunks) && \
                              ((x) < &old_chunks[HEAP_SIZE]))
#define in_chunk_range(x) (in_busy_chunk_range(x) || in_old_chunk_range(x))

/*bool*/
int pair_tag_matches(datum o);
int closure_tag_matches(datum o);
int array_tag_matches(datum arr);
int bytes_tag_matches(datum bytes);
int str_tag_matches(datum bytes);
int broken_heart_tag_matches(datum bh);

/*bool*/
#define pairp(x) (in_chunk_range(x) && pair_tag_matches(x))

#define arrayp(x) (in_chunk_range(x) && array_tag_matches(x))

#define bytesp(x) (in_chunk_range(x) && bytes_tag_matches(x))

#define strp(x) (in_chunk_range(x) && str_tag_matches(x))

#define closurep(x) (in_chunk_range(x) && closure_tag_matches(x))

#define broken_heartp(x) (in_old_chunk_range(x) && broken_heart_tag_matches(x))

#define nil (0)

void become(datum *a, datum *b, int keep_b);

#endif /*mem_h*/
