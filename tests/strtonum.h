/*
 * Declaration of strtonum(3) for non-OpenBSD test hosts.
 *
 * OpenBSD libc declares strtonum in <stdlib.h> unconditionally; on
 * other hosts the test support object provides the implementation and
 * this header supplies the prototype.  It is force-included only when
 * building the host tests.
 */

#ifndef OPENBAR_TEST_STRTONUM_H
#define OPENBAR_TEST_STRTONUM_H

#ifndef __OpenBSD__
long long strtonum(const char *, long long, long long, const char **);
#endif

#endif /* OPENBAR_TEST_STRTONUM_H */
