#include <stdio.h>

int
main(int argc, char **argv)
{
    int n = 0;

    for (;;) {
        n = n + 1;

        if ((n % 100000) == 0) printf("%d\n", n);
    }
}
