#ifndef GEN_H
#define GEN_H

#include <stdlib.h>

typedef size_t * datum;

#define int2datum(x) ((datum) (((x) << 1) | 1))
#define datum2int(d) (((int) (d)) >> 1)
#define intp(x) (((uint) (x)) & 1)

int truep(datum d);

void die(const char *m);
datum die1(const char *m, datum d);

#define max(a,b) (((a)>(b))?(a):(b))
#define min(a,b) (((a)<(b))?(a):(b))

#define assert(b) ((b) ? 0 : die("b failed"))

datum memq(datum d, datum alist);
datum assq(datum d, datum alist);

#endif /*GEN_H*/
