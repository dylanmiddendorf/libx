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

/* This internal data structure is utilized to prevent duplication within the
   string constant pool. The forward declaration is intentional to strongly
   discourage external usage of these internal mechanisms. */
typedef struct _scp_set scp_set_t;

typedef struct _strpool
{
  scp_set_t *index;
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
const char *scp_insert_string (strpool_t *pool, char *s);
void scp_insert_string_len (strpool_t *pool, char *str, size_t n);
void scp_insert_string_range (strpool_t *pool, char *str, size_t from,
                              size_t to);

/* ----- String Pool Diagnostic Functions ----- */
size_t scp_size (strpool_t *pool);
size_t scp_memory_usage (strpool_t *pool);

#endif /* STRPOOL_H */