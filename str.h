/* str.h - str type header */

#ifndef str_h
#define str_h

typedef struct str {
    size_t size;
    char data[];
} *str;

void str_init();

int strp(datum x);

datum make_str_init(size_t size, const char *bytes);
datum make_str_init_permanent(size_t size, const char *bytes);

size_t copy_str_contents(char *dest, datum str, size_t n);
size_t copy_str_contents0(char *dest, datum str, size_t n);

#endif /*str_h*/
