/*
 * Minimal host test framework for the portable openbar units.
 */

#ifndef OPENBAR_TEST_H
#define OPENBAR_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int	test_failures;

#define CHECK(cond) do {					\
	if (!(cond)) {						\
		fprintf(stderr, "%s:%d: check failed: %s\n",	\
		    __FILE__, __LINE__, #cond);			\
		test_failures++;				\
	}							\
} while (0)

#define CHECK_STR(a, b) do {					\
	const char *_a = (a);					\
	const char *_b = (b);					\
	if (_a == NULL || _b == NULL || strcmp(_a, _b) != 0) {	\
		fprintf(stderr,				\
		    "%s:%d: check failed: \"%s\" != \"%s\"\n",	\
		    __FILE__, __LINE__,				\
		    _a == NULL ? "(null)" : _a,			\
		    _b == NULL ? "(null)" : _b);		\
		test_failures++;				\
	}							\
} while (0)

#define TEST_MAIN()						\
int								\
main(void)							\
{								\
	run_tests();						\
	if (test_failures != 0) {				\
		fprintf(stderr, "%d check(s) failed\n",		\
		    test_failures);				\
		return 1;					\
	}							\
	puts("ok");						\
	return 0;						\
}

#endif /* OPENBAR_TEST_H */
