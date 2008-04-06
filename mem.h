/* mem.h - lisp-style data structure support */

#ifndef mem_h
#define mem_h

#include "gen.h"

typedef void(*na_fn_free)(void *);

typedef struct chunk {
    uint info;
    datum datums[];
} *chunk;

typedef struct pair {
    uint info;
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
    uint info;
    datum env;
    method_table table;
} *closure;

typedef struct str {
    uint info;
    size_t size;
    size_t len;
    char data[];
} *str;

datum cons(datum x, datum y);
datum make_array(uint len);
datum make_bytes(uint len);
datum make_bytes_init(const char *s);
datum make_bytes_init_len(const char *s, int len);
datum make_str(size_t size, size_t len);
datum make_str_init(size_t size, size_t len, const char *bytes);
datum make_closure(datum env, uint *table);
datum grow_closure(datum *o, uint len, na_fn_free fn, void *data);


datum array_get(datum arr, uint index);
datum array_put(datum arr, uint index, datum val);
uint array_len(datum arr);

size_t bytes_len(datum bytes);
char *bytes_contents(datum bytes);
size_t copy_bytes_contents(char *dest, datum bytes, size_t n);
size_t copy_bytes_contents0(char *dest, datum bytes, size_t n);

size_t copy_str_contents(char *dest, datum str, size_t n);
size_t copy_str_contents0(char *dest, datum str, size_t n);

inline pair datum2pair(datum d);
inline str datum2str(datum d);

inline closure datum2closure(datum d);

#define car(x) (datum2pair(x)->car)
#define cdr(x) (datum2pair(x)->cdr)

#define caar(x) (car(car(x)))
#define cadr(x) (car(cdr(x)))
#define cdar(x) (cdr(car(x)))
#define cddr(x) (cdr(cdr(x)))

#define caddr(x) (car(cddr(x)))

#define cdaddr(x) (cdr(caddr(x)))

#define item0(x) (array_get((x),0))
#define item1(x) (array_get((x),1))
#define item2(x) (array_get((x),2))
#define item00(x) (item0(item0(x)))
#define item01(x) (item0(item1(x)))
#define item10(x) (item1(item0(x)))
#define item11(x) (item1(item1(x)))
#define item011(x) (item0(item11(x)))
#define item0011(x) (item0(item011(x)))
#define item1011(x) (item1(item011(x)))

#define setitem0(x,y) (array_put((x),0,(y)))
#define setitem1(x,y) (array_put((x),1,(y)))

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

#define nil ((chunk)0)

void become(datum *a, datum *b, int keep_b);

#endif /*mem_h*/
