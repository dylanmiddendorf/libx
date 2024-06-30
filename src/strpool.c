/*
 * strpool.c - [description]
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "strpool.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* TODO: Finish doxygen comments for relevent functions */
/* TODO: Add logging framework for debugging and warnings */
/* TODO: Shift scp_set capacities from powers of two towards primes */
/* TODO: Shift hashing algorithms from djb2 towards crc32 */

#define SCP_DEFAULT_INITIAL_CAPACITY 16

/* Source: https://doi.org/10.1145/358728.358745 */
#define SCP_SET_DEFAULT_INITIAL_CAPACITY 16
#define SCP_SET_DEFAULT_CELLAR_RATIO 0.14
#define SCP_SET_DEFAULT_LOAD_FACTOR 0.68

typedef struct _scp_bucket
{
  const char *key;

  uint32_t hash;
  uint32_t next;
} scp_bucket_t;

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

static void scp_ensure_capacity (strpool_t *pool, uint32_t min);
static uint32_t scp_new_capacity (strpool_t *pool, uint32_t min_capacity);

static scp_set_t *_scp_set_new ();
static scp_set_t *_scp_set_init (scp_set_t *index);
static scp_set_t *_scp_set_init_custom (scp_set_t *set, uint32_t capacity,
                                        float load_factor, float cellar_ratio);
static void _scp_set_free (scp_set_t *set);

static inline bool _scp_set_add (scp_set_t *set, const char *s);
static inline bool _scp_set_contains (scp_set_t *set, const char *s);
static inline const char *_scp_set_get (scp_set_t *set, const char *s);
static inline bool _scp_set_is_empty (scp_set_t *set);

static scp_set_t *_scp_set_rehash (scp_set_t *set);
static scp_bucket_t *_scp_bucket_find (scp_set_t *set, const char *s,
                                       bool create, int *rflags);
static inline bool _scp_bucket_is_empty (scp_bucket_t *bucket);

/* Source: http://www.cse.yorku.ca/~oz/hash.html */
static uint32_t _scp_set_djb2 (const char *s);

/**
 * @brief Terminates the program execution due to a critical exception.
 *
 * This function prints an error message to the standard error stream
 * based on the provided format string and its arguments, then terminates
 * the program with a failure status code.
 *
 * @param fmt Format string for the error message, followed by optional
 * arguments.
 * @param ... Optional arguments corresponding to the format string.
 */
_Noreturn static void
_die (const char *fmt, ...)
{
  va_list arg;

  va_start (arg, fmt);
  vfprintf (stderr, fmt, arg);
  fprintf (stderr, "\n");
  va_end (arg);

  exit (EXIT_FAILURE);
}

strpool_t *
scp_new ()
{
  strpool_t *pool = malloc (sizeof *pool);
  if (!pool)
    _die ("%s: Unable to allocate string pool (errno=%d).", __func__, errno);
  pool->_dynamic = true;

  /* The primary purpose of this function is to dynamically allocate the
     string constant pool. Specifically, `scp_new(...)` has no obligation
     to initalize the structure; as a result, the data contained within the
     strpool_t structure can be initalized within `scp_init(...)`. */
  return pool;
}

strpool_t *
scp_init (strpool_t *pool)
{
  if (!pool) /* Ensure that the pool is properly allocated */
    pool = scp_new ();

  pool->index = _scp_set_init (NULL);

  pool->capacity = SCP_DEFAULT_INITIAL_CAPACITY;
  pool->pool = malloc (pool->capacity);
  if (!pool->pool)
    _die ("%s: Unable to allocate pool->pool (errno=%d)", __func__, errno);

  return pool;
}

void
scp_free (strpool_t *pool)
{
  _scp_set_free (pool->index);

  if (pool->pool)
    free (pool->pool);

  /* Prevent stack-based pools from causing trouble :) */
  if (pool->_dynamic)
    free (pool);
}

const char *
scp_insert_string (strpool_t *pool, char *s)
{
  uint32_t str_len;
  const char *pooled_str = _scp_set_get (pool->index, s);

  /* If the string pool doesn't contain the string, insert the string */
  if (!pooled_str)
    {
      /* Cache string length to prevent repeat calls */
      str_len = strnlen (s, UINT32_MAX);
      scp_ensure_capacity (pool, pool->size + str_len);

      strcpy (pool->pool + pool->size, s);
      pool->pool[pool->size + str_len] = '\0';
      _scp_set_add (pool->index, pool->pool + pool->size);

      pool->size += str_len + 1; /* Null terminator */
    }

  return pooled_str;
}

void
scp_insert_string_len (strpool_t *pool, char *str, size_t n)
{
}

void
scp_insert_string_range (strpool_t *pool, char *str, size_t from, size_t to)
{
}

inline size_t
scp_size (strpool_t *pool)
{
  return pool->index->size;
}

inline size_t
scp_memory_usage (strpool_t *pool)
{
  size_t table_size = (sizeof (*pool->index->table) * pool->index->size);
  size_t set_size = table_size + sizeof (*pool->index);

  return (pool->capacity + sizeof (*pool)) + set_size;
}

static void
scp_ensure_capacity (strpool_t *pool, uint32_t min_capacity)
{
  if(min_capacity < pool->capacity)
    return;
  uint32_t new_capacity = scp_new_capacity (pool, min_capacity);
  pool->pool = realloc (pool->pool, new_capacity);
  if (!pool->pool)
    _die ("%s: Unable to allocate pool->pool (errno=%d)", __func__, errno);
}

static uint32_t
scp_new_capacity (strpool_t *pool, uint32_t min_capacity)
{
  uint32_t old_capacity = pool->capacity;
  uint32_t new_capacity = (old_capacity << 1) + 2;

  if (min_capacity < old_capacity)
    _die ("%s: String pool capacity has overflowed.", __func__);
  if (new_capacity < old_capacity)
    return UINT32_MAX; /* Prevent overflow by capping the capacity */

  return new_capacity > min_capacity ? new_capacity : min_capacity;
}

static scp_set_t *
_scp_set_new ()
{
  scp_set_t *set = malloc (sizeof (*set));
  if (!set)
    _die ("%s: Unable to allocate set (errno=%d)", __func__, errno);
  set->_dynamic = true;

  return set;
}

static inline scp_set_t *
_scp_set_init (scp_set_t *set)
{
  /* Prevent duplicate code by initalizing with library defaults */
  return _scp_set_init_custom (set, SCP_SET_DEFAULT_INITIAL_CAPACITY,
                               SCP_SET_DEFAULT_LOAD_FACTOR,
                               SCP_SET_DEFAULT_CELLAR_RATIO);
}

static scp_set_t *
_scp_set_init_custom (scp_set_t *set, uint32_t capacity, float load_factor,
                      float cellar_ratio)
{
  uint32_t i; /* Index for clearing the table's "pointers" */

  if (!set)
    set = _scp_set_new ();

  set->capacity = capacity;
  set->table = calloc (set->capacity, sizeof (*set->table));
  if (!set->table)
    _die ("%s: Unable to allocate set->table (errno=%d)", __func__, errno);

  /* Since the `next` member represents the next bucket's index, -1U is used
     to represent an invalid index, instead of the expected NULL (0). */
  for (i = 0; i < set->capacity; ++i)
    set->table[i].next = -1U;
  set->size = 0U, set->cellar_size = 0U;

  set->load_factor = load_factor;
  set->cellar_ratio = cellar_ratio;
  set->cellar_capacity = set->capacity * set->cellar_ratio;
  set->table_capacity = set->capacity - set->cellar_capacity;
  return set;
}

static void
_scp_set_free (scp_set_t *set)
{
  if (set->table)
    free (set->table);

  if (set->_dynamic)
    free (set);
}

/**
 * @brief Adds a specified element to a set.
 *
 * This function attempts to add the provided element to the given set. It
 * checks whether the element already exists within the set. If the element is
 * not found, it is added to the set.
 *
 * @param[in] set A pointer to the hash set in which the element is to be
 * added.
 * @param[in] s The element (as a string) to be added to the hash set.
 *
 * @return `true` if the element was previously in the set, `false` otherwise.
 */
static inline bool
_scp_set_add (scp_set_t *set, const char *s)
{
  int rflags; /* Used to determine if a bucket was added */
  _scp_bucket_find (set, s, true, &rflags); /* Attempt to create the bucket */
  return rflags & 0x01;
}

static inline bool
_scp_set_contains (scp_set_t *set, const char *s)
{
  return _scp_bucket_find (set, s, false, NULL) == NULL;
}

static inline const char *
_scp_set_get (scp_set_t *set, const char *s)
{
  scp_bucket_t *b = _scp_bucket_find (set, s, false, NULL);
  return b ? b->key : NULL;
}

static inline bool
_scp_set_is_empty (scp_set_t *set)
{
  return set->size == 0;
}

static scp_set_t *
_scp_set_rehash (scp_set_t *set)
{
  uint32_t i; /* Iterating through the old table */
  scp_bucket_t *old_table = set->table;
  uint32_t old_capacity = set->capacity;

  /* Reinitalize the set with an increased size... */
  _scp_set_init_custom (set, set->capacity << 1, set->load_factor,
                        set->cellar_ratio);

  /* Shift all the entries between the two tables */
  for (i = 0; i < old_capacity; ++i)
    if (old_table[i].key)
      _scp_set_add (set, old_table[i].key);

  free (old_table); /* Prevent memory leaks */
  return set;
}

static scp_bucket_t *
_scp_bucket_find (scp_set_t *set, const char *s, bool create, int *rflags)
{
  uint32_t hash;
  /* The variable `chain` is utilized primarily for searching for buckets
     within the initial coalesced chain. Conversely, the variable `next` is
     designated for use exclusively when a new bucket is being created. In such
     cases, during the initialization of the bucket, the `chain` bucket will be
     linked to the `next` bucket. */
  scp_bucket_t *chain, *next = NULL;

  if (!set || !s) /* Loosely check for null pointer exceptions */
    return NULL;

  if (set->size > (set->capacity * set->load_factor))
    return _scp_bucket_find (_scp_set_rehash (set), s, create, rflags);

  hash = _scp_set_djb2 (s);
  chain = set->table + (hash % set->table_capacity);
  if (!_scp_bucket_is_empty (chain))
    {
      while (true)
        {
          /* The requested key exists (and was found) */
          if (hash == chain->hash && strcmp (s, chain->key) == 0)
            {
              if (rflags)
                *rflags &= ~0x01;
              return chain;
            }

          /* Iterate through the remainder of the chain */
          if (chain->next == -1U)
            break;
          chain = set->table + chain->next;
        }
    }

  /* The key doesn't exist in this set. */
  if (!create)
    return NULL;

  /* Special case: start of chain... */
  if (_scp_bucket_is_empty (chain))
    goto bucket_init;

  /* Attempt to store the bucket in the cellar first. */
  if (set->cellar_size < set->cellar_capacity)
    {
      /* (--set->cellar_size) -- maximal-munch principle */
      next = set->table + (set->capacity - (--set->cellar_size));
      goto bucket_init;
    }

  next = chain; /* Start linearly proabing after the chain */
  do
    {
      /* idex = (pBucket[n] - pTable[0]) / sizeof(scp_bucket_t) */
      next = set->table + ((next - set->table + 1) % set->table_capacity);
    }
  while (!_scp_bucket_is_empty (next) && chain != next);

  if (chain == next)
    {
      if (set->size < set->capacity)
        _die ("%s: size < capacity, yet no buckets could be found.", __func__);

      /* Print a waring (shift to logging) */
      fprintf (stderr, "%s: No buckets could be found (set->load_factor=%f)",
               __func__, set->load_factor);

      set->load_factor = SCP_SET_DEFAULT_LOAD_FACTOR;
      return _scp_bucket_find (_scp_set_rehash (set), s, create, rflags);
    }

bucket_init:
  set->size++; /* Increase size for rehashing... */
  chain->key = s, chain->hash = hash;
  if (next) /* Only expand the chain if `next` is valid */
    chain->next = next - set->table;

  if (rflags)
    *rflags |= 0x01;
  return next;
}

/**
 * @brief Checks if the bucket is empty.
 *
 * This function checks whether the specified bucket is empty.
 *
 * @param bucket A pointer to the bucket to check.
 * @return `true` if the bucket is empty, `false` otherwise.
 *
 * @note It is the responsibility of the caller to make sure argument(s) are
 *       checked before this method is called.
 */
static inline bool
_scp_bucket_is_empty (scp_bucket_t *bucket)
{
  return !bucket->key && bucket->next == -1;
}

static uint32_t
_scp_set_djb2 (const char *s)
{
  uint32_t hash = 5381U;
  char c; /* Used to store the current character */
  if (!s) /* Defend against pesky null pointers */
    return 0U;

  while ((c = *s++))
    hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
  return hash;
}
