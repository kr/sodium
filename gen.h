#ifndef GEN_H
#define GEN_H

typedef void * datum;

#define INT_TAG 0x1
#define int2datum(x) ((datum) (((x) << 1) | INT_TAG))
#define datum2int(d) (((int) (d)) >> 1)
#define intp(x) (((uint) (x)) & INT_TAG)

int truep(datum d);

void die(const char *m);
datum die1(const char *m, datum d);

#define max(a,b) (((a)>(b))?(a):(b))
#define min(a,b) (((a)<(b))?(a):(b))

#define assert(b) ((b) ? 0 : die("b failed"))

datum memq(datum d, datum alist);
datum assq(datum d, datum alist);

#endif /*GEN_H*/
