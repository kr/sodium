/* pair.h - pair type header */

#ifndef pair_h
#define pair_h

extern datum pair_mtab;

typedef struct pair {
    datum car, cdr;
} *pair;

void pair_init();

datum cons(datum x, datum y);

inline pair datum2pair(datum d);

int pairp(datum x);

#define car(x) (datum2pair(x)->car)
#define cdr(x) (datum2pair(x)->cdr)

#define caar(x) (car(car(x)))
#define cadr(x) (car(cdr(x)))
#define cdar(x) (cdr(car(x)))
#define cddr(x) (cdr(cdr(x)))

#define caddr(x) (car(cddr(x)))

#define cdaddr(x) (cdr(caddr(x)))

#endif /*pair_h*/
