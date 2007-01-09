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
datum make_blank(uint len);

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
#define caddr(x) car(cdr(cdr(x)))
#define caaddr(x) car(car(cdr(cdr(x))))
#define cdaddr(x) cdr(car(cdr(cdr(x))))

#define MAX_PAIRS 1024

extern struct pair *busy_pairs;

void init_mem(void);

/*bool*/
#define in_pair_range(x) ((((pair)(x)) >= busy_pairs) && \
                          (((pair)(x)) < &busy_pairs[MAX_PAIRS]))

#define BOX_MASK 0x3
#define PAIR_TAG 0x0

/*bool*/
#define pair_sig_matches(x) ((((unsigned int)(x)) & BOX_MASK) == PAIR_TAG)

int array_tag_matches(datum arr);
int string_tag_matches(datum str);
int blank_tag_matches(datum str);

/*bool*/
#define pairp(x) (in_pair_range(x) && \
                  pair_sig_matches(x) && \
                  array_tag_matches(x))

#define stringp(x) (in_pair_range(x) && \
                    pair_sig_matches(x) && \
                    string_tag_matches(x))

#define blankp(x) (in_pair_range(x) && \
                    pair_sig_matches(x) && \
                    blank_tag_matches(x))

#define nil ((pair)0)

#endif /*PAIR_H*/
