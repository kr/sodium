/* bytes.h - bytes type header */

#ifndef bytes_h
#define bytes_h

extern datum bytes_mtab;

void bytes_init();

datum make_bytes(uint len);
datum make_bytes_init(const char *s);
datum make_bytes_init_len(const char *s, int len);

size_t copy_bytes_contents(char *dest, datum bytes, size_t n);
size_t copy_bytes_contents0(char *dest, datum bytes, size_t n);
char *bytes_contents(datum bytes);

int bytesp(datum x);

#endif /*bytes_h*/
