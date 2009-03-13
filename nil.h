/* nil.h - nil type header */

#ifndef nil_h
#define nil_h

struct nil_struct {
    datum desc;
    datum mtab;
    void *payload[0];
};

extern struct nil_struct nil_s;
#define nil ((datum) &nil_s.payload)

void nil_init();

#endif /*nil_h*/
