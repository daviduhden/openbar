/*
 * Minimal host test framework for the portable openbar units.
 *
 * TEST_MAIN() pins the C locale the same way the production program
 * does, so test results are independent of LANG/LC_* in the host
 * environment.
 */

#ifndef OPENBAR_TEST_H
#define OPENBAR_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../openbar.h"

static int test_failures;

#define CHECK(cond)                                                            \
	do {                                                                   \
		if (!(cond)) {                                                 \
			fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, \
			    __LINE__, #cond);                                  \
			test_failures++;                                       \
		}                                                              \
	} while (0)

#define CHECK_STR(a, b)                                                        \
	do {                                                                   \
		typeof((a) + 0) _a = (a);                                      \
		typeof((b) + 0) _b = (b);                                      \
		if (_a == nullptr || _b == nullptr || strcmp(_a, _b) != 0) {   \
			fprintf(stderr,                                        \
			    "%s:%d: check failed: \"%s\" != \"%s\"\n",         \
			    __FILE__, __LINE__, _a == nullptr ? "(null)" : _a, \
			    _b == nullptr ? "(null)" : _b);                    \
			test_failures++;                                       \
		}                                                              \
	} while (0)

#define TEST_MAIN()                                                            \
	int main(void)                                                         \
	{                                                                      \
		if (locale_init() == -1) {                                     \
			fprintf(stderr, "cannot set the C locale\n");          \
			return 1;                                              \
		}                                                              \
		run_tests();                                                   \
		if (test_failures != 0) {                                      \
			fprintf(                                               \
			    stderr, "%d check(s) failed\n", test_failures);    \
			return 1;                                              \
		}                                                              \
		puts("ok");                                                    \
		return 0;                                                      \
	}

#endif /* OPENBAR_TEST_H */
