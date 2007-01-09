#include <stdio.h>

int
main(int argc, char **argv)
{
    int n = 0;

    for (; n < 5000000;) {
        n = n + 1;

        if ((n % 100000) == 0) printf("%d\n", n);
    }

    return 0;
}
