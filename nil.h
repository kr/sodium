/* nil.h - nil type header */

#ifndef nil_h
#define nil_h

struct nil_struct {
    size_t desc;
    datum mtab;
    void *payload[0];
};

extern struct nil_struct nil_s;
#define nil ((datum) &nil_s.payload)

#endif /*nil_h*/
