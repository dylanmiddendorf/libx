/*
 * strpool.h - [description]
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef STRPOOL_H
#define STRPOOL_H 1

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Due to inherent limitations of the C language, `scp_set_t` cannot be forward
 * declared and inlined within the string constant pool structure. Nonetheless,
 * `scp_bucket_t` is purposefully a forward declaration to strongly discourage
 * external use of the internal mechanisms.
 */
typedef struct _scp_bucket scp_bucket_t;

typedef struct _scp_set
{
  scp_bucket_t *table;

  uint32_t capacity;
  uint32_t table_capacity;
  uint32_t cellar_capacity;

  uint32_t size; /* Number of active entries in whole map (table & cellar) */
  uint32_t cellar_size; /* Number of active entries in strictly in cellar */

  float load_factor;
  float cellar_ratio;

  bool _dynamic;
} scp_set_t;

typedef struct _strpool
{
  scp_set_t index;
  char *pool;

  uint32_t capacity;
  uint32_t size;

  /* This field serves as a safeguard for de-allocation. Specifically, it is
   set within the `scp_new(...)` function to indicate dynamic allocation.
   When this flag is set and the `scp_free(...)` function is called, the
   function will free both the members and the entire data structure. */
  bool _dynamic;
} strpool_t;

/* ----- String Pool Allocation Functions ----- */
strpool_t *scp_new ();
strpool_t *scp_init (strpool_t *pool);
void scp_free (strpool_t *pool);

/* ----- String Pool Insertion Functions ------ */
const char *scp_insert_string (strpool_t *pool, const char *s);
const char *scp_insert_string_len (strpool_t *pool, const char *str, size_t n);

/* ----- String Pool Diagnostic Functions ----- */
uint32_t scp_size (strpool_t *pool);
size_t scp_memory_usage (strpool_t *pool);

#endif /* STRPOOL_H */