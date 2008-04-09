/* str.h - str type header */

#ifndef str_h
#define str_h

typedef struct str {
    uint info;
    size_t size;
    size_t len;
    char data[];
} *str;

datum make_str_init(size_t size, size_t len, const char *bytes);

inline str datum2str(datum d);

size_t copy_str_contents(char *dest, datum str, size_t n);
size_t copy_str_contents0(char *dest, datum str, size_t n);

#endif /*str_h*/
