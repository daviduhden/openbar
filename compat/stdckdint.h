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

/*
 * <stdckdint.h> compatibility shim.
 *
 * openbar uses the checked integer arithmetic macros from C23's
 * <stdckdint.h>.  Clang accepts -std=c23, but the header belongs to
 * the C library, and a system can have a C23-capable compiler while
 * its libc still lacks the header (OpenBSD at the time of writing).
 * Because compat/ is on the include path (-Icompat), this file
 * shadows the system header:
 *
 *   - when a real <stdckdint.h> is reachable after compat/ in the
 *     include search order, it is included and used unchanged;
 *   - otherwise ckd_add(), ckd_sub() and ckd_mul() are provided with
 *     the compiler's overflow builtins.
 *
 * __has_include_next() and #include_next (instead of __has_include()
 * and #include) are essential here: compat/ precedes the system
 * directories, so a plain include would find this very file again and
 * recurse instead of reaching the real header.  include_next resumes
 * the search after compat/, which cannot match this file.  Both are
 * GCC/Clang extensions; they are confined to this portability shim,
 * and the fallback below relies on those same compilers anyway.
 *
 * OPENBAR_STDCKDINT_FORCE_FALLBACK exists so that the test suite can
 * exercise the fallback on hosts whose libc already provides the
 * header.  The build never defines it.
 */

#ifndef OPENBAR_COMPAT_STDCKDINT_H
#define OPENBAR_COMPAT_STDCKDINT_H

#ifndef OPENBAR_STDCKDINT_FORCE_FALLBACK
#if defined(__has_include_next)
#if __has_include_next(<stdckdint.h>)
/*
 * GCC diagnoses the (intentional) GNU #include_next extension under
 * -Wpedantic; suppress exactly that diagnostic for this one directive
 * so the shim stays warning-free.  This is not hiding a defect: the
 * directive is the mechanism that reaches the real header.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include_next <stdckdint.h>
#pragma GCC diagnostic pop
#endif
#endif
#endif /* !OPENBAR_STDCKDINT_FORCE_FALLBACK */

/*
 * A conforming C23 <stdckdint.h> defines __STDC_VERSION_STDCKDINT_H__
 * (C23 7.20).  If it is not defined here, either no real header was
 * found or it is not conforming, so supply the operations.
 *
 * C23 specifies ckd_add(result, a, b) as computing the mathematical
 * (infinite precision) value of a + b, converting it to the type of
 * *result, storing it in *result, and returning true exactly when the
 * conversion is not value-preserving.  The operands are not first
 * converted to a common type.  __builtin_add_overflow(a, b, result)
 * has that exact contract: it promotes a and b to infinite precision,
 * converts the mathematical result to the type of *result, stores it
 * and reports whether the conversion changed the value.  GCC's own
 * <stdckdint.h> is defined in these very terms, so this fallback
 * matches the real header for the standard integer types used by the
 * project.  Each operand is evaluated exactly once.
 */
#ifndef __STDC_VERSION_STDCKDINT_H__
#if defined(__has_builtin)
#if !__has_builtin(__builtin_add_overflow) || \
    !__has_builtin(__builtin_sub_overflow) || \
    !__has_builtin(__builtin_mul_overflow)
#error "openbar needs the compiler overflow builtins for <stdckdint.h>"
#endif
#elif !defined(__GNUC__) && !defined(__clang__)
#error "openbar needs __builtin_*_overflow for <stdckdint.h>"
#endif

/*
 * The #ifndef guards keep the fallback from colliding with a partial
 * or non-conforming header that already defines some of the macros.
 */
#ifndef ckd_add
#define ckd_add(result, a, b) \
	((_Bool)__builtin_add_overflow((a), (b), (result)))
#endif
#ifndef ckd_sub
#define ckd_sub(result, a, b) \
	((_Bool)__builtin_sub_overflow((a), (b), (result)))
#endif
#ifndef ckd_mul
#define ckd_mul(result, a, b) \
	((_Bool)__builtin_mul_overflow((a), (b), (result)))
#endif

#define __STDC_VERSION_STDCKDINT_H__ 202311L
#endif /* !__STDC_VERSION_STDCKDINT_H__ */

#endif /* OPENBAR_COMPAT_STDCKDINT_H */
