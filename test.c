/* to support:
 * - types declaration and reference
 * -> cleanup code, make video and publish!
 */

#include "test.h"

#define MYCONST 10

#define add(a, b) (a + b)

struct bar {
    char c;
};

struct foo {
	int amember;
	int b;
	int c;
    int d;
    int e;
    struct bar bb;
};

typedef struct foo bar;

const int myconst2 = MYCONST;

/**
 * Documentation for ga
 */
struct foo ga = {
    .amember = myconst2,
    .b = 4,
};

int testFunc(int a, bar b)
{
    struct foo aFoo = {
        .amember = 10,
        .b = 5,
    };

    aFoo.amember = (a +  b.c);

    aFoo.amember = add(a, b.c);

    for (int i = 0; i < 10; i++)
        testFunc(b.c + i + myconst2, ga);
    
    return b.amember + a;
}

int testFunc2(struct foo aFoo)
{
    return aFoo.amember + myconst2;
}
