#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include "gen.h"
#include "prim.h"
#include "obj.h"
#include "array.h"
#include "bytes.h"
#include "symbol.h"
#include "vm.h"

#define VOID ((void) 0)

/* global functions */

static void
pr_array(datum d)
{
    uint i, len;
    if (d == nil) return;
    prx(array_get(d, 0));
    len = array_len(d);
    for (i = 1; i < len; i++) {
        write(1, " ", 1);
        prx(array_get(d, i));
    }
}

static void
pr_pair(datum d, int sp)
{
    if (d == nil) return;
    if (sp) write(1, " ", 1);
    prx(car(d));
    if (pairp(cdr(d)) || cdr(d) == nil) {
        pr_pair(cdr(d), 1);
    } else {
        write(1, " . ", 3);
        prx(cdr(d));
    }
}

/* r must be <= 36 */
static void
pradix(int fd, unsigned int i, int r, int print_zero)
{
    char c = "0123456789abcdefghijklmnopqrstuvwxyz"[i % r];
    if (!i) return (print_zero? write(fd, "0", 1) : 0), VOID;
    pradix(fd, i / r, r, 0);
    write(fd, &c, 1);
}

void
prfmt(int fd, char *fmt, ...)
{
    va_list ap;
    char *p, *sval;
    int ival;
    unsigned int uval;

    va_start(ap, fmt);
    for (p = fmt; *p; p++) {
        if (*p != '%') {
            write(fd, p, 1);
            continue;
        }
        switch (*++p) {
            case 'u':
                uval = va_arg(ap, unsigned int);
                pradix(fd, uval, 10, 1);
                break;
            case 'x':
                uval = va_arg(ap, unsigned int);
                write(fd, "0x", 2);
                pradix(fd, uval, 16, 1);
                break;
            case 'd':
                ival = va_arg(ap, int);
                if (ival < 0) write(1, "-", 1);
                pradix(fd, ival > 0? ival: -ival, 10, 1);
                break;
            case 'p':
                uval = va_arg(ap, unsigned int);
                if (uval) {
                    write(fd, "0x", 2);
                    pradix(fd, uval, 16, 1);
                } else {
                    write(fd, "(nil)", 5);
                }
                break;
            case 's':
                sval = va_arg(ap, char *);
                write(fd, sval, strlen(sval));
                break;
            default:
                write(fd, p, 1);
                break;
        }
    }
    va_end(ap);
}

static void
pr_bytes(datum d)
{
    char *b = (char *) d;
    uint i, len;
    prfmt(1, "%d", b[0]);
    len = datum_size(d);
    for (i = 1; i < len; i++) {
        prfmt(1, " %d", b[i]);
    }
}

void
prx(datum d)
{
    if (d == nil) {
        write(1, "()", 2);
    } else if (intp(d)) {
        int i = datum2int(d);
        prfmt(1, "%d", i);
    } else if (addrp(d)) {
        prfmt(1, "<addr %p>", d);
    } else if (symbolp(d)) {
        pr_symbol(d);
    } else if (strp(d)) {
        write(1, d, datum_size(d));
    } else if (bytesp(d)) {
        write(1, "(bytes ", 7);
        pr_bytes(d);
        write(1, ")", 1);
    } else if (pairp(d)) {
        write(1, "(", 1);
        pr_pair(d, 0);
        write(1, ")", 1);
    } else if (arrayp(d)) {
        write(1, "(array ", 7);
        pr_array(d);
        write(1, ")", 1);
    } else if (broken_heartp(d)) {
        d = (datum) *d;
        prfmt(1, "<broken-heart %p>", d);
        prx(d);
    } else {
        prfmt(1, "<unknown-object %p>", d);
    }
}

void
pr(datum d)
{
    prx(d);
    write(1, "\n", 1);
}
