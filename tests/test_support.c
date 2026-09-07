/*
 * Test-harness support code: strtonum(3) for non-OpenBSD test hosts.
 * On OpenBSD the native libc implementation is used and this object
 * is empty.  Compiled only into the test binaries.
 */

#include <errno.h>
#include <limits.h>
#include <stdlib.h>

#ifndef __OpenBSD__
long long
strtonum(const char *numstr, long long minval, long long maxval,
    const char **errstrp)
{
	long long	 ll = 0;
	const char	*errmsg = "invalid";
	char		*ep;

	if (minval > maxval) {
		errmsg = "invalid range";
		ll = 0;
		goto done;
	}
	errno = 0;
	ll = strtoll(numstr, &ep, 10);
	if (numstr == ep || *ep != '\0')
		ll = 0;
	else if ((ll == LLONG_MIN && errno == ERANGE) || ll < minval) {
		errmsg = "too small";
		ll = minval;
	} else if ((ll == LLONG_MAX && errno == ERANGE) || ll > maxval) {
		errmsg = "too large";
		ll = maxval;
	} else {
		errmsg = NULL;
	}
done:
	if (errstrp != NULL)
		*errstrp = errmsg;
	return ll;
}
#endif
