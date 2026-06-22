#pragma once

#include "types.h"

/* ── Utility macros ─────────────────────────────────────────── */

#define ARRAY_SIZE(a)   (sizeof(a) / sizeof((a)[0]))
#define ALIGN_UP(v, a)  (((v) + (a) - 1) & ~((a) - 1))
#define ALIGN_DOWN(v,a) ((v) & ~((a) - 1))
#define BIT(n)          (1UL << (n))
#define MIN(a, b)       ((a) < (b) ? (a) : (b))
#define MAX(a, b)       ((a) > (b) ? (a) : (b))
#define UNUSED(x)       ((void)(x))

/* ── Debug assert (halts with file:line on failure) ─────────── */

void _kassert_fail(const char *expr, const char *file, int line);

#ifdef NDEBUG
#  define KASSERT(cond)  ((void)0)
#else
#  define KASSERT(cond) \
     do { if (!(cond)) _kassert_fail(#cond, __FILE__, __LINE__); } while (0)
#endif

/* ── Common attribute shorthands ────────────────────────────── */

#define __packed        __attribute__((packed))
#define __noreturn      __attribute__((noreturn))
#define __aligned(n)    __attribute__((aligned(n)))
#define __used          __attribute__((used))
