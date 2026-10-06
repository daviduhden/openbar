/*
 * Self-test for the <stdckdint.h> fallback in compat/stdckdint.h.
 *
 * The fallback is forced (see run.sh) so that it is exercised even on
 * hosts whose compiler or libc already ships <stdckdint.h>.  The
 * cases cover the standard integer types openbar actually feeds to
 * ckd_add()/ckd_sub()/ckd_mul() (size_t, long long, unsigned long
 * long, time_t) plus the properties that distinguish the C23 contract
 * from a naive a + b: representability is judged against the result
 * type, operands are not converted to a common type first, and each
 * argument is evaluated exactly once.
 *
 * Exits non-zero if any check fails.
 */

/*
 * Copyright (c) 2024 Gonzalo Rodriguez <gonzalo@x61.sh>
 * Copyright (c) 2024-2026 David Uhden Collado <david@uhden.dev>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <time.h>

#include <stdckdint.h>

static int failures;

static void
check(int condition, const char *what)
{
	if (!condition) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

/* Single-evaluation probes. */
static unsigned probe_calls;

static unsigned
probe(void)
{
	probe_calls++;
	return (1u);
}

static size_t	slots[4];
static unsigned slot_index;

static size_t *
next_slot(void)
{
	return (&slots[slot_index++]);
}

int
main(void)
{
	size_t		z;
	long long	s;
	unsigned long long q;
	time_t		t;
	_Bool		ov;

	/* ---- size_t: the unsigned type used for allocation sizes ---- */
	z = 0;
	ov = ckd_add(&z, (size_t)SIZE_MAX, (size_t)0);
	check(!ov && z == SIZE_MAX, "size_t add max+0 does not overflow");

	ov = ckd_add(&z, (size_t)SIZE_MAX, (size_t)1);
	check(ov, "size_t add max+1 overflows");

	ov = ckd_mul(&z, (size_t)SIZE_MAX, (size_t)1);
	check(!ov && z == SIZE_MAX, "size_t mul max*1 does not overflow");

	ov = ckd_mul(&z, (size_t)SIZE_MAX, (size_t)2);
	check(ov, "size_t mul max*2 overflows");

	ov = ckd_sub(&z, (size_t)0, (size_t)1);
	check(ov, "size_t sub 0-1 overflows");

	ov = ckd_sub(&z, (size_t)SIZE_MAX, (size_t)SIZE_MAX);
	check(!ov && z == 0, "size_t sub max-max is zero");

	/* ---- long long: signed deadlines and refresh deltas ---- */
	{
		const long long ll_max = LLONG_MAX;
		const long long ll_min = LLONG_MIN;

		s = 0;
		ov = ckd_add(&s, ll_max, 0LL);
		check(!ov && s == ll_max, "long long add max+0 does not overflow");

		ov = ckd_add(&s, ll_max, 1LL);
		check(ov, "long long add max+1 overflows");

		ov = ckd_sub(&s, ll_min, 1LL);
		check(ov, "long long sub min-1 overflows");

		ov = ckd_sub(&s, ll_min, ll_min);
		check(!ov && s == 0, "long long sub min-min is zero");

		ov = ckd_mul(&s, ll_min, -1LL);
		check(ov, "long long mul min*-1 overflows");
	}

	/* ---- unsigned long long: the memory report product ---- */
	q = 0;
	ov = ckd_mul(&q, (unsigned long long)ULLONG_MAX, 1ULL);
	check(!ov && q == ULLONG_MAX, "unsigned long long mul max*1");

	ov = ckd_mul(&q, (unsigned long long)ULLONG_MAX, 2ULL);
	check(ov, "unsigned long long mul max*2 overflows");

	/* ---- time_t: refresh schedule subtractions ---- */
	t = (time_t)5;
	ov = ckd_sub(&q, t, (time_t)2);
	check(!ov && q == 3, "time_t subtraction into wide result");

	/* ---- representability is judged against the result type ---- */
	{
		unsigned short h = 0;

		ov = ckd_add(&h, 30000, 30000);
		check(!ov && h == 60000,
		    "unsigned short add 30000+30000 fits");

		ov = ckd_add(&h, 40000, 40000);
		check(ov, "unsigned short add 40000+40000 overflows");
	}

	/* ---- operands are promoted to infinite precision, not to a
	 *      common operand type ---- */
	q = 0;
	ov = ckd_add(&q, (unsigned)UINT_MAX, 1u);
	check(!ov && q == (unsigned long long)UINT_MAX + 1ULL,
	    "unsigned long long add UINT_MAX+1 does not overflow");

	ov = ckd_mul(&q, (unsigned)UINT_MAX, 2u);
	check(!ov && q == (unsigned long long)UINT_MAX * 2ULL,
	    "unsigned long long mul UINT_MAX*2 does not overflow");

	/* ---- each argument is evaluated exactly once ---- */
	probe_calls = 0;
	(void)ckd_add(&z, probe(), probe());
	check(probe_calls == 2, "operands are evaluated once each");

	slot_index = 0;
	(void)ckd_add(next_slot(), (size_t)1, (size_t)2);
	check(slot_index == 1, "result pointer is evaluated once");
	check(slots[0] == 3, "result was stored through the pointer");

	/* ---- return value and result type ---- */
	z = 0;
	ov = ckd_add(&z, (size_t)1, (size_t)2);
	check(ov == 0 && z == 3, "success returns false and stores result");

	if (failures != 0) {
		printf("%d stdckdint check(s) failed\n", failures);
		return (1);
	}
	printf("stdckdint fallback: all checks passed\n");
	return (0);
}
