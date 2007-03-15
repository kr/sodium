/* pair.h - lisp-style data structure support */

#ifndef PAIR_H
#define PAIR_H

#include "gen.h"

typedef struct pair {
    uint info;
    datum datums[];
} *pair;

datum cons(datum x, datum y);
datum make_array(uint len);
datum make_string(uint len);
datum make_string_init(const char *s);
datum make_string_init_len(const char *s, int len);
datum make_obj(uint len);
datum make_compiled_obj(datum env, uint *table);


datum array_get(datum arr, uint index);
void array_put(datum arr, uint index, datum val);
uint array_len(datum arr);

char *string_contents(datum str);

/* caller must free the string returned by this function */
char *copy_string_contents(datum str);

#define checked_car(x) array_get((x),0)
#define checked_cdr(x) array_get((x),1)
#define checked_cadr(x) checked_car(checked_cdr(x))
#define checked_cddr(x) checked_cdr(checked_cdr(x))
#define checked_caddr(x) checked_car(checked_cddr(x))

// int /*bool*/ pairp(datum x);
#define car(x) (((pair) (x))->datums[0])
#define cdr(x) (((pair) (x))->datums[1])
#define caar(x) car(car(x))
#define cadr(x) car(cdr(x))
#define cdar(x) cdr(car(x))
#define cddr(x) cdr(cdr(x))
#define caddr(x) car(cdr(cdr(x)))
#define caaddr(x) car(car(cdr(cdr(x))))
#define cdaddr(x) cdr(car(cdr(cdr(x))))

#define HEAP_SIZE (2 * 1024 * 1024)

extern struct pair *busy_pairs, *old_pairs;

void init_mem(void);

/*bool*/
#define in_busy_pair_range(x) ((((pair)(x)) >= busy_pairs) && \
                               (((pair)(x)) < &busy_pairs[HEAP_SIZE]))
#define in_old_pair_range(x) (old_pairs && (((pair)(x)) >= old_pairs) && \
                              (((pair)(x)) < &old_pairs[HEAP_SIZE]))
#define in_pair_range(x) (in_busy_pair_range(x) || in_old_pair_range(x))

/*bool*/
int array_tag_matches(datum arr);
int string_tag_matches(datum str);
int obj_tag_matches(datum o);
int broken_heart_tag_matches(datum bh);

/*bool*/
#define pairp(x) (in_pair_range(x) && array_tag_matches(x))

#define stringp(x) (in_pair_range(x) && string_tag_matches(x))

#define objp(x) (in_pair_range(x) && obj_tag_matches(x))

#define broken_heartp(x) (in_old_pair_range(x) && broken_heart_tag_matches(x))

#define nil ((pair)0)

void dump_obj(datum o);

#endif /*PAIR_H*/
