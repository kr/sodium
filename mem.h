/* mem.h - lisp-style data structure support */

#ifndef mem_h
#define mem_h

#include "gen.h"

typedef void(*na_fn_free)(void *);

typedef struct chunk {
    uint info;
    datum datums[];
} *chunk;

datum cons(datum x, datum y);
datum make_array(uint len);
datum make_bytes(uint len);
datum make_bytes_init(const char *s);
datum make_bytes_init_len(const char *s, int len);
datum make_closure(datum env, uint *table);
datum grow_closure(datum *o, uint len, na_fn_free fn, void *data);


datum array_get(datum arr, uint index);
void array_put(datum arr, uint index, datum val);
uint array_len(datum arr);

char *bytes_contents(datum str);

/* caller must free the bytes returned by this function */
char *copy_bytes_contents(datum str);

#define checked_car(x) array_get((x),0)
#define checked_cdr(x) array_get((x),1)
#define checked_cadr(x) checked_car(checked_cdr(x))
#define checked_cddr(x) checked_cdr(checked_cdr(x))
#define checked_caddr(x) checked_car(checked_cddr(x))

// int /*bool*/ arrayp(datum x);
#define car(x) (((chunk) (x))->datums[0])
#define cdr(x) (((chunk) (x))->datums[1])
#define caar(x) car(car(x))
#define cadr(x) car(cdr(x))
#define cdar(x) cdr(car(x))
#define cddr(x) cdr(cdr(x))
#define caddr(x) car(cdr(cdr(x)))
#define caaddr(x) car(car(cdr(cdr(x))))
#define cdaddr(x) cdr(car(cdr(cdr(x))))

#define HEAP_SIZE (2 * 1024 * 1024)

extern struct chunk *busy_chunks, *old_chunks;

void init_mem(void);

/*bool*/
#define in_busy_chunk_range(x) ((((chunk)(x)) >= busy_chunks) && \
                               (((chunk)(x)) < &busy_chunks[HEAP_SIZE]))
#define in_old_chunk_range(x) (old_chunks && (((chunk)(x)) >= old_chunks) && \
                              (((chunk)(x)) < &old_chunks[HEAP_SIZE]))
#define in_chunk_range(x) (in_busy_chunk_range(x) || in_old_chunk_range(x))

/*bool*/
int array_tag_matches(datum arr);
int bytes_tag_matches(datum str);
int closure_tag_matches(datum o);
int broken_heart_tag_matches(datum bh);

/*bool*/
#define arrayp(x) (in_chunk_range(x) && array_tag_matches(x))

#define bytesp(x) (in_chunk_range(x) && bytes_tag_matches(x))

#define closurep(x) (in_chunk_range(x) && closure_tag_matches(x))

#define broken_heartp(x) (in_old_chunk_range(x) && broken_heart_tag_matches(x))

#define nil ((chunk)0)

void become(datum *a, datum *b, int keep_b);

#endif /*mem_h*/
