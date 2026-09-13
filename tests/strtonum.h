/*
 * Declaration of strtonum(3) for the test builds.
 *
 * The prototype is always supplied: on non-OpenBSD hosts the test
 * support object provides the implementation, and on OpenBSD the
 * POSIX feature-test macros used by the tests hide the libc
 * declaration in <stdlib.h> even though the symbol is still exported.
 * This header is force-included only when building the host tests.
 */

#ifndef OPENBAR_TEST_STRTONUM_H
#define OPENBAR_TEST_STRTONUM_H

long long strtonum(const char *, long long, long long, const char **);

#endif /* OPENBAR_TEST_STRTONUM_H */
