/* bytes.h - bytes type header */

#ifndef bytes_h
#define bytes_h

typedef struct bytes {
    size_t size;
    char data[];
} *bytes;

datum make_bytes_init(const char *s);
datum make_bytes_init_len(const char *s, int len);

inline bytes datum2bytes(datum d);

size_t copy_bytes_contents(char *dest, datum bytes, size_t n);
size_t copy_bytes_contents0(char *dest, datum bytes, size_t n);

#endif /*bytes_h*/
