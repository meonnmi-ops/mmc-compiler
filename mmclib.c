/*
 * ============================================================================
 *  mmclib.c  -  MMC C Runtime Library Implementation
 * ============================================================================
 *
 *  Project:    MMC Compiler  -  Self-Hosted Runtime
 *  Context:    အေအိုင်AI / Nyanlin-AI
 *
 *  Description:
 *      Full C implementation of the MMC runtime library (mmclib.h).
 *      Provides Python-like dynamic types, strings, arrays, dicts,
 *      memory management, file I/O, and Myanmar Unicode support.
 *
 *  Architecture:
 *      MMC Source (.mmc) -> mmc_c_codegen.mmc -> C99 (+mmclib.h) -> Binary
 *
 *  Key Implementation Details:
 *      - Arena allocator: bump allocation with linked blocks (256 KB each)
 *      - String results: arena when available, rotating static buffers otherwise
 *      - Dict: djb2 hash, open addressing, linear probing, tombstone deletes
 *      - Array: realloc-based growable (doubles capacity when full)
 *      - UTF-8: Myanmar chars are 3-byte sequences (U+1000-U+109F)
 *      - Myanmar digits: U+1040-U+1049 <-> ASCII 0x30-0x39
 *
 *  Compatibility:
 *      - C99 (GCC / Clang / MSVC)
 *      - Linux, macOS, Windows, Android (NDK), iOS
 *      - Compiles clean with gcc -std=c99 -Wall
 *
 *  Version:    2.0.0
 *  License:    MIT
 *  Author:     MMC Compiler Team / Nyanlin-AI
 * ============================================================================
 */

/* Enable POSIX.1-2008 features (strdup, setenv, nanosleep, etc.)
 * Must be defined before ANY standard header inclusion. */
#ifndef _WIN32
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include "mmclib.h"

/* ---- Standard C Headers ---- */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>

/* ---- Platform-Specific Headers ---- */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#include <sys/time.h>
#include <sys/stat.h>
#endif


/* ============================================================================
 *  Internal State and Helpers
 * ============================================================================ */

/* Global arena pointer (NULL until mmc_runtime_init) */
static MMC_Arena *g_mmc_arena = NULL;

/* Last error code */
static MMC_Error g_mmc_last_error = MMC_OK;

/* ---- Static String Buffer Pool ---- *
 * Rotating set of static buffers for transient string results.
 * When the global arena is available, arena allocation is preferred
 * (strings survive until arena reset).  When no arena, static buffers
 * are used (overwritten on subsequent calls).
 */
#define MMC_NUM_STATIC_BUFS   8
#define MMC_STATIC_BUF_SIZE   65536

static char mmc_static_bufs[MMC_NUM_STATIC_BUFS][MMC_STATIC_BUF_SIZE];
static int  mmc_static_buf_next = 0;

static char *mmc_static_buf(void)
{
    char *buf = mmc_static_bufs[mmc_static_buf_next];
    mmc_static_buf_next = (mmc_static_buf_next + 1) % MMC_NUM_STATIC_BUFS;
    return buf;
}

/* ---- Result Buffer Allocator ---- *
 * Prefers arena when available (persistent until arena reset),
 * falls back to static buffer (transient, overwritten), then malloc.
 */
static char *mmc_result_buf(size_t needed)
{
    MMC_Arena *arena = mmc_get_global_arena();
    if (arena && needed > 0 && needed <= MMC_ARENA_BLOCK_SIZE) {
        return (char *)mmc_arena_alloc(arena, needed);
    }
    if (needed <= MMC_STATIC_BUF_SIZE) {
        return mmc_static_buf();
    }
    return (char *)malloc(needed);
}

/* Safe strncpy that always null-terminates */
static void mmc_safe_strcpy(char *dst, const char *src, size_t maxlen)
{
    if (!src) { dst[0] = '\0'; return; }
    size_t len = strlen(src);
    if (len >= maxlen) len = maxlen - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}


/* ============================================================================
 *  UTF-8 Helpers (Internal)
 * ============================================================================ */

/* Decode one UTF-8 character from **pp, advance pointer, return codepoint.
 * Returns 0 on end-of-string. */
static int mmc_utf8_decode(const char **pp)
{
    const unsigned char *p = (const unsigned char *)*pp;
    int cp;

    if (p[0] == 0) return 0;

    if (p[0] < 0x80) {
        cp = p[0];
        *pp += 1;
    } else if ((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
        cp = ((int)(p[0] & 0x1F) << 6) | (p[1] & 0x3F);
        *pp += 2;
    } else if ((p[0] & 0xF0) == 0xE0 &&
               (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        cp  = ((int)(p[0] & 0x0F) << 12);
        cp |= ((int)(p[1] & 0x3F) << 6);
        cp |=  (int)(p[2] & 0x3F);
        *pp += 3;
    } else if ((p[0] & 0xF8) == 0xF0 &&
               (p[1] & 0xC0) == 0x80 &&
               (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
        cp  = ((int)(p[0] & 0x07) << 18);
        cp |= ((int)(p[1] & 0x3F) << 12);
        cp |= ((int)(p[2] & 0x3F) << 6);
        cp |=  (int)(p[3] & 0x3F);
        *pp += 4;
    } else {
        /* Invalid leading byte - skip one byte */
        cp = (int)p[0];
        *pp += 1;
    }
    return cp;
}

/* Encode a Unicode codepoint as UTF-8 into buf.  Returns bytes written. */
static int mmc_utf8_encode(char *buf, int cp)
{
    if (cp < 0x80) {
        buf[0] = (char)cp;
        return 1;
    } else if (cp < 0x800) {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    } else if (cp < 0x10000) {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    } else {
        buf[0] = (char)(0xF0 | (cp >> 18));
        buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
}

/* Advance byte pointer by n Unicode characters */
static const char *mmc_utf8_advance(const char *s, long n)
{
    long i;
    for (i = 0; i < n && *s; i++) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x80)       s += 1;
        else if (c < 0xE0)  s += 2;
        else if (c < 0xF0)  s += 3;
        else                 s += 4;
    }
    return s;
}

/* Count Unicode codepoints in a UTF-8 string */
static size_t mmc_utf8_count(const char *s)
{
    size_t count = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        if (*p < 0x80)       p += 1;
        else if (*p < 0xE0)  p += 2;
        else if (*p < 0xF0)  p += 3;
        else                 p += 4;
        count++;
    }
    return count;
}


/* ============================================================================
 *  Section 6: Memory Management (Arena Allocator)
 *
 *  Implemented first because string functions and value operations depend on it.
 * ============================================================================ */

MMC_Arena *mmc_arena_new(void)
{
    MMC_Arena *arena = (MMC_Arena *)malloc(sizeof(MMC_Arena));
    if (!arena) return NULL;
    arena->current = NULL;
    arena->total_allocated = 0;
    return arena;
}

void *mmc_arena_alloc(MMC_Arena *arena, size_t size)
{
    if (!arena) return NULL;

    /* Align to 8 bytes for safe pointer access */
    size = (size + 7) & ~(size_t)7;

    /* If no current block or not enough space, allocate a new block */
    if (!arena->current ||
        (arena->current->used + size > arena->current->size)) {
        size_t block_size = MMC_ARENA_BLOCK_SIZE;
        if (size > block_size) block_size = size;

        MMC_Arena_Block *block =
            (MMC_Arena_Block *)malloc(sizeof(MMC_Arena_Block));
        if (!block) return NULL;

        block->data = (char *)malloc(block_size);
        if (!block->data) {
            free(block);
            return NULL;
        }

        block->used = 0;
        block->size = block_size;
        block->next = arena->current;
        arena->current = block;
    }

    char *ptr = arena->current->data + arena->current->used;
    arena->current->used += size;
    arena->total_allocated += size;
    return ptr;
}

char *mmc_arena_strdup(MMC_Arena *arena, const char *s)
{
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *copy = (char *)mmc_arena_alloc(arena, len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

void mmc_arena_reset(MMC_Arena *arena)
{
    if (!arena) return;
    MMC_Arena_Block *block = arena->current;
    while (block) {
        MMC_Arena_Block *next = block->next;
        free(block->data);
        free(block);
        block = next;
    }
    arena->current = NULL;
    arena->total_allocated = 0;
}

void mmc_arena_free(MMC_Arena *arena)
{
    mmc_arena_reset(arena);
    free(arena);
}

MMC_Arena *mmc_get_global_arena(void)
{
    return g_mmc_arena;
}

void mmc_global_arena_reset(void)
{
    if (g_mmc_arena) {
        mmc_arena_reset(g_mmc_arena);
    }
}


/* ============================================================================
 *  Section 2: MMC Dynamic Value Type
 * ============================================================================ */

/* ---- Value Constructors ---- */

MMC_Value mmc_int(long long v)
{
    MMC_Value val;
    val.type = MMC_TYPE_INT;
    val.int_val = v;
    return val;
}

MMC_Value mmc_float(double v)
{
    MMC_Value val;
    val.type = MMC_TYPE_FLOAT;
    val.float_val = v;
    return val;
}

MMC_Value mmc_string(const char *s)
{
    MMC_Value val;
    val.type = MMC_TYPE_STRING;
    val.str_val = s ? s : "";
    return val;
}

MMC_Value mmc_bool(int v)
{
    MMC_Value val;
    val.type = MMC_TYPE_BOOL;
    val.bool_val = v ? 1 : 0;
    return val;
}

MMC_Value mmc_none(void)
{
    MMC_Value val;
    val.type = MMC_TYPE_NONE;
    val.int_val = 0;
    return val;
}

MMC_Value mmc_array(void)
{
    MMC_Value val;
    val.type = MMC_TYPE_ARRAY;
    val.arr_val = mmc_arr_new();
    return val;
}

MMC_Value mmc_dict(void)
{
    MMC_Value val;
    val.type = MMC_TYPE_DICT;
    val.dict_val = mmc_dict_new();
    return val;
}

/* ---- Value Accessors ---- */

long long mmc_as_int(MMC_Value v)
{
    switch (v.type) {
        case MMC_TYPE_INT:    return v.int_val;
        case MMC_TYPE_FLOAT:  return (long long)v.float_val;
        case MMC_TYPE_BOOL:   return v.bool_val ? 1LL : 0LL;
        case MMC_TYPE_STRING: {
            const char *s = v.str_val;
            if (!s || !*s) return 0;
            return strtoll(s, NULL, 10);
        }
        default: return 0;
    }
}

double mmc_as_float(MMC_Value v)
{
    switch (v.type) {
        case MMC_TYPE_FLOAT:  return v.float_val;
        case MMC_TYPE_INT:    return (double)v.int_val;
        case MMC_TYPE_BOOL:   return v.bool_val ? 1.0 : 0.0;
        case MMC_TYPE_STRING: {
            const char *s = v.str_val;
            if (!s || !*s) return 0.0;
            return strtod(s, NULL);
        }
        default: return 0.0;
    }
}

const char *mmc_as_string(MMC_Value v)
{
    return mmc_value_to_str(v);
}

int mmc_as_bool(MMC_Value v)
{
    switch (v.type) {
        case MMC_TYPE_BOOL:   return v.bool_val;
        case MMC_TYPE_INT:    return v.int_val != 0;
        case MMC_TYPE_FLOAT:  return v.float_val != 0.0;
        case MMC_TYPE_STRING: return v.str_val != NULL && v.str_val[0] != '\0';
        case MMC_TYPE_ARRAY:  return v.arr_val != NULL && v.arr_val->len > 0;
        case MMC_TYPE_DICT:   return v.dict_val != NULL && v.dict_val->count > 0;
        case MMC_TYPE_NONE:   return 0;
        default: return 0;
    }
}

/* ---- Type Checking ---- */

int mmc_is_int(MMC_Value v)    { return v.type == MMC_TYPE_INT; }
int mmc_is_float(MMC_Value v)  { return v.type == MMC_TYPE_FLOAT; }
int mmc_is_string(MMC_Value v) { return v.type == MMC_TYPE_STRING; }
int mmc_is_bool(MMC_Value v)   { return v.type == MMC_TYPE_BOOL; }
int mmc_is_none(MMC_Value v)   { return v.type == MMC_TYPE_NONE; }
int mmc_is_array(MMC_Value v)  { return v.type == MMC_TYPE_ARRAY; }
int mmc_is_dict(MMC_Value v)   { return v.type == MMC_TYPE_DICT; }

/* ---- Type Name ---- */

const char *mmc_type_name(MMC_Value v)
{
    switch (v.type) {
        case MMC_TYPE_INT:    return "int";
        case MMC_TYPE_FLOAT:  return "float";
        case MMC_TYPE_STRING: return "str";
        case MMC_TYPE_BOOL:   return "bool";
        case MMC_TYPE_NONE:   return "none";
        case MMC_TYPE_ARRAY:  return "array";
        case MMC_TYPE_DICT:   return "dict";
        default:              return "unknown";
    }
}

/* ---- Value Equality ---- */

int mmc_values_equal(MMC_Value a, MMC_Value b)
{
    /* Numeric equality: INT vs FLOAT or FLOAT vs INT */
    if (a.type == MMC_TYPE_INT && b.type == MMC_TYPE_FLOAT)
        return (double)a.int_val == b.float_val;
    if (a.type == MMC_TYPE_FLOAT && b.type == MMC_TYPE_INT)
        return a.float_val == (double)b.int_val;

    if (a.type != b.type) return 0;

    switch (a.type) {
        case MMC_TYPE_INT:    return a.int_val == b.int_val;
        case MMC_TYPE_FLOAT:  return a.float_val == b.float_val;
        case MMC_TYPE_STRING: return strcmp(a.str_val, b.str_val) == 0;
        case MMC_TYPE_BOOL:   return a.bool_val == b.bool_val;
        case MMC_TYPE_NONE:   return 1;
        case MMC_TYPE_ARRAY: {
            if (a.arr_val == b.arr_val) return 1;
            if (!a.arr_val || !b.arr_val) return 0;
            if (a.arr_val->len != b.arr_val->len) return 0;
            {
                size_t i;
                for (i = 0; i < a.arr_val->len; i++) {
                    if (!mmc_values_equal(a.arr_val->data[i], b.arr_val->data[i]))
                        return 0;
                }
            }
            return 1;
        }
        case MMC_TYPE_DICT: {
            if (a.dict_val == b.dict_val) return 1;
            if (!a.dict_val || !b.dict_val) return 0;
            if (a.dict_val->count != b.dict_val->count) return 0;
            /* Compare all keys of a against b */
            {
                size_t i;
                for (i = 0; i < a.dict_val->capacity; i++) {
                    MMC_Dict_Entry *e = &a.dict_val->buckets[i];
                    if (e->occupied == 1) {
                        MMC_Value bval = mmc_dict_get(b.dict_val, e->key);
                        if (!mmc_values_equal(e->value, bval)) return 0;
                    }
                }
            }
            return 1;
        }
        default: return 0;
    }
}

/* ---- Value to String (recursive with depth limit) ---- */

static void mmc_value_to_str_buf(char *buf, size_t bufsize, MMC_Value v, int depth)
{
    if (depth > MMC_MAX_NEST_DEPTH) {
        snprintf(buf, bufsize, "...");
        return;
    }

    switch (v.type) {
        case MMC_TYPE_INT:
            snprintf(buf, bufsize, "%lld", v.int_val);
            break;
        case MMC_TYPE_FLOAT: {
            /* Show decimal point for round floats: 3.0 not 3 */
            double d = v.float_val;
            if (d == floor(d) && fabs(d) < 1e15 && !isinf(d)) {
                snprintf(buf, bufsize, "%.1f", d);
            } else {
                snprintf(buf, bufsize, "%g", d);
            }
            break;
        }
        case MMC_TYPE_STRING:
            snprintf(buf, bufsize, "%s", v.str_val ? v.str_val : "");
            break;
        case MMC_TYPE_BOOL:
            snprintf(buf, bufsize, "%s", v.bool_val ? "true" : "false");
            break;
        case MMC_TYPE_NONE:
            snprintf(buf, bufsize, "None");
            break;
        case MMC_TYPE_ARRAY: {
            MMC_Array *arr = v.arr_val;
            if (!arr) { snprintf(buf, bufsize, "[]"); return; }

            size_t pos = 0;
            buf[pos++] = '[';
            {
                size_t i;
                for (i = 0; i < arr->len && pos < bufsize - 2; i++) {
                    if (i > 0) buf[pos++] = ',';
                    buf[pos++] = ' ';
                    mmc_value_to_str_buf(buf + pos, bufsize - pos,
                                         arr->data[i], depth + 1);
                    pos += strlen(buf + pos);
                }
            }
            if (pos < bufsize - 1) buf[pos++] = ']';
            buf[pos] = '\0';
            break;
        }
        case MMC_TYPE_DICT: {
            MMC_Dict *dict = v.dict_val;
            if (!dict) { snprintf(buf, bufsize, "{}"); return; }

            size_t pos = 0;
            buf[pos++] = '{';
            int first = 1;
            {
                size_t i;
                for (i = 0; i < dict->capacity && pos < bufsize - 2; i++) {
                    MMC_Dict_Entry *e = &dict->buckets[i];
                    if (e->occupied != 1) continue;
                    if (!first) { buf[pos++] = ','; buf[pos++] = ' '; }
                    first = 0;
                    snprintf(buf + pos, bufsize - pos, "%s: ", e->key);
                    pos += strlen(buf + pos);
                    mmc_value_to_str_buf(buf + pos, bufsize - pos,
                                         e->value, depth + 1);
                    pos += strlen(buf + pos);
                }
            }
            if (pos < bufsize - 1) buf[pos++] = '}';
            buf[pos] = '\0';
            break;
        }
        default:
            snprintf(buf, bufsize, "<unknown>");
            break;
    }
}

const char *mmc_value_to_str(MMC_Value v)
{
    char *buf = mmc_result_buf(MMC_STATIC_BUF_SIZE);
    mmc_value_to_str_buf(buf, MMC_STATIC_BUF_SIZE, v, 0);
    return buf;
}

/* ---- Print / Println ---- */

void mmc_print_value(MMC_Value v)
{
    const char *s = mmc_value_to_str(v);
    printf("%s", s);
}

void mmc_println_value(MMC_Value v)
{
    const char *s = mmc_value_to_str(v);
    printf("%s\n", s);
}


/* ============================================================================
 *  Section 3: MMC Dynamic Array
 * ============================================================================ */

MMC_Array *mmc_arr_new(void)
{
    return mmc_arr_new_capacity(MMC_ARR_INIT_SIZE);
}

MMC_Array *mmc_arr_new_capacity(size_t cap)
{
    MMC_Array *arr = (MMC_Array *)malloc(sizeof(MMC_Array));
    if (!arr) return NULL;
    if (cap < 1) cap = MMC_ARR_INIT_SIZE;
    arr->data = (MMC_Value *)calloc(cap, sizeof(MMC_Value));
    if (!arr->data) { free(arr); return NULL; }
    arr->len = 0;
    arr->capacity = cap;
    return arr;
}

void mmc_arr_free(MMC_Array *arr)
{
    if (!arr) return;
    free(arr->data);
    free(arr);
}

void mmc_arr_clear(MMC_Array *arr)
{
    if (!arr) return;
    arr->len = 0;
}

/* Internal: ensure capacity for at least one more element */
static int mmc_arr_ensure(MMC_Array *arr, size_t needed)
{
    if (!arr) return MMC_ERR_INDEX;
    if (arr->len + needed <= arr->capacity) return MMC_OK;
    while (arr->capacity < arr->len + needed)
        arr->capacity *= 2;
    MMC_Value *newdata =
        (MMC_Value *)realloc(arr->data, arr->capacity * sizeof(MMC_Value));
    if (!newdata) return MMC_ERR_MEMORY;
    arr->data = newdata;
    return MMC_OK;
}

MMC_Value mmc_arr_get(MMC_Array *arr, size_t index)
{
    if (!arr || index >= arr->len) {
        mmc_set_last_error(MMC_ERR_INDEX);
        return mmc_none();
    }
    return arr->data[index];
}

int mmc_arr_set(MMC_Array *arr, size_t index, MMC_Value val)
{
    if (!arr || index >= arr->len) {
        mmc_set_last_error(MMC_ERR_INDEX);
        return MMC_ERR_INDEX;
    }
    arr->data[index] = val;
    return MMC_OK;
}

MMC_Value mmc_arr_front(MMC_Array *arr)
{
    return mmc_arr_get(arr, 0);
}

MMC_Value mmc_arr_back(MMC_Array *arr)
{
    if (!arr || arr->len == 0) return mmc_none();
    return arr->data[arr->len - 1];
}

int mmc_arr_push(MMC_Array *arr, MMC_Value val)
{
    if (mmc_arr_ensure(arr, 1) != MMC_OK) return MMC_ERR_MEMORY;
    arr->data[arr->len++] = val;
    return MMC_OK;
}

MMC_Value mmc_arr_pop(MMC_Array *arr)
{
    if (!arr || arr->len == 0) {
        mmc_set_last_error(MMC_ERR_INDEX);
        return mmc_none();
    }
    return arr->data[--arr->len];
}

int mmc_arr_insert(MMC_Array *arr, size_t index, MMC_Value val)
{
    if (!arr) return MMC_ERR_INDEX;
    if (index > arr->len) index = arr->len;
    if (mmc_arr_ensure(arr, 1) != MMC_OK) return MMC_ERR_MEMORY;
    /* Shift elements right */
    size_t i;
    for (i = arr->len; i > index; i--) {
        arr->data[i] = arr->data[i - 1];
    }
    arr->data[index] = val;
    arr->len++;
    return MMC_OK;
}

int mmc_arr_remove(MMC_Array *arr, size_t index)
{
    if (!arr || index >= arr->len) {
        mmc_set_last_error(MMC_ERR_INDEX);
        return MMC_ERR_INDEX;
    }
    /* Shift elements left */
    size_t i;
    for (i = index; i + 1 < arr->len; i++) {
        arr->data[i] = arr->data[i + 1];
    }
    arr->len--;
    return MMC_OK;
}

int mmc_arr_append_all(MMC_Array *arr, MMC_Array *other)
{
    if (!arr || !other) return MMC_ERR_INDEX;
    if (other->len == 0) return MMC_OK;
    if (mmc_arr_ensure(arr, other->len) != MMC_OK) return MMC_ERR_MEMORY;
    memcpy(arr->data + arr->len, other->data,
           other->len * sizeof(MMC_Value));
    arr->len += other->len;
    return MMC_OK;
}

long mmc_arr_index_of(MMC_Array *arr, MMC_Value val)
{
    if (!arr) return -1;
    {
        size_t i;
        for (i = 0; i < arr->len; i++) {
            if (mmc_values_equal(arr->data[i], val)) return (long)i;
        }
    }
    return -1;
}

int mmc_arr_contains(MMC_Array *arr, MMC_Value val)
{
    return mmc_arr_index_of(arr, val) >= 0;
}

int mmc_arr_count(MMC_Array *arr, MMC_Value val)
{
    if (!arr) return 0;
    int count = 0;
    {
        size_t i;
        for (i = 0; i < arr->len; i++) {
            if (mmc_values_equal(arr->data[i], val)) count++;
        }
    }
    return count;
}

size_t mmc_arr_len(MMC_Array *arr)
{
    return arr ? arr->len : 0;
}

int mmc_arr_is_empty(MMC_Array *arr)
{
    return arr ? (arr->len == 0) : 1;
}

void mmc_arr_reverse(MMC_Array *arr)
{
    if (!arr || arr->len < 2) return;
    size_t i, j;
    for (i = 0, j = arr->len - 1; i < j; i++, j--) {
        MMC_Value tmp = arr->data[i];
        arr->data[i] = arr->data[j];
        arr->data[j] = tmp;
    }
}

/* Sorting comparators for qsort */

static int mmc_int_cmp(const void *a, const void *b)
{
    long long va = ((const MMC_Value *)a)->int_val;
    long long vb = ((const MMC_Value *)b)->int_val;
    if (va < vb) return -1;
    if (va > vb) return 1;
    return 0;
}

static int mmc_float_cmp(const void *a, const void *b)
{
    double va = ((const MMC_Value *)a)->float_val;
    double vb = ((const MMC_Value *)b)->float_val;
    if (va < vb) return -1;
    if (va > vb) return 1;
    return 0;
}

void mmc_arr_sort_int(MMC_Array *arr)
{
    if (!arr || arr->len < 2) return;
    qsort(arr->data, arr->len, sizeof(MMC_Value), mmc_int_cmp);
}

void mmc_arr_sort_float(MMC_Array *arr)
{
    if (!arr || arr->len < 2) return;
    qsort(arr->data, arr->len, sizeof(MMC_Value), mmc_float_cmp);
}

MMC_Array *mmc_arr_slice(MMC_Array *arr, long start, long end)
{
    if (!arr) return mmc_arr_new();
    long len = (long)arr->len;

    /* Handle negative indices (Python-style) */
    if (start < 0) start += len;
    if (end < 0) end += len;
    if (start < 0) start = 0;
    if (end > len) end = len;
    if (start >= end) return mmc_arr_new();

    MMC_Array *result = mmc_arr_new_capacity((size_t)(end - start));
    {
        long i;
        for (i = start; i < end; i++) {
            mmc_arr_push(result, arr->data[(size_t)i]);
        }
    }
    return result;
}


/* ============================================================================
 *  Section 4: MMC Dictionary (Hash Map)
 * ============================================================================ */

/* ---- djb2 Hash Function ---- */

static unsigned long mmc_djb2_hash(const char *str)
{
    unsigned long hash = 5381;
    int c;
    while ((c = (unsigned char)*str++)) {
        hash = ((hash << 5) + hash) + (unsigned long)c;  /* hash * 33 + c */
    }
    return hash;
}

/* Tombstone sentinel value for deleted slots */
#define MMC_DICT_TOMBSTONE  2

/* Maximum load factor before resize: 7/10 = 0.7 */
#define MMC_DICT_MAX_LOAD_NUM   7
#define MMC_DICT_MAX_LOAD_DEN  10

/* Internal: resize dict to new capacity (must be power-of-2 for fast mod) */
static int mmc_dict_resize(MMC_Dict *dict, size_t new_cap)
{
    MMC_Dict_Entry *new_buckets;
    size_t i;

    if (!dict) return MMC_ERR_INDEX;

    new_buckets = (MMC_Dict_Entry *)calloc(new_cap, sizeof(MMC_Dict_Entry));
    if (!new_buckets) return MMC_ERR_MEMORY;

    /* Rehash all active entries */
    for (i = 0; i < dict->capacity; i++) {
        MMC_Dict_Entry *e = &dict->buckets[i];
        if (e->occupied != 1) continue;

        unsigned long h = mmc_djb2_hash(e->key);
        size_t idx = (size_t)(h & (new_cap - 1));

        /* Linear probing */
        while (new_buckets[idx].occupied == 1) {
            idx = (idx + 1) & (new_cap - 1);
        }
        new_buckets[idx] = *e;
    }

    free(dict->buckets);
    dict->buckets = new_buckets;
    dict->capacity = new_cap;
    return MMC_OK;
}

/* Internal: check if resize is needed and perform it */
static int mmc_dict_maybe_resize(MMC_Dict *dict)
{
    if (!dict) return MMC_ERR_INDEX;
    /* Check load factor: count * DEN > capacity * NUM */
    if (dict->count * MMC_DICT_MAX_LOAD_DEN >
        dict->capacity * MMC_DICT_MAX_LOAD_NUM) {
        size_t new_cap = dict->capacity * 2;
        return mmc_dict_resize(dict, new_cap);
    }
    return MMC_OK;
}

/* Internal: round up to next power of 2 */
static size_t mmc_next_pow2(size_t v)
{
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    if (sizeof(size_t) > 4) v |= v >> 32;
    return v + 1;
}

MMC_Dict *mmc_dict_new(void)
{
    return mmc_dict_new_capacity(MMC_DICT_INIT_SIZE);
}

MMC_Dict *mmc_dict_new_capacity(size_t cap)
{
    MMC_Dict *dict = (MMC_Dict *)malloc(sizeof(MMC_Dict));
    if (!dict) return NULL;
    cap = mmc_next_pow2(cap);
    dict->buckets = (MMC_Dict_Entry *)calloc(cap, sizeof(MMC_Dict_Entry));
    if (!dict->buckets) { free(dict); return NULL; }
    dict->count = 0;
    dict->capacity = cap;
    return dict;
}

void mmc_dict_free(MMC_Dict *dict)
{
    if (!dict) return;
    /* Free heap-allocated keys */
    {
        size_t i;
        for (i = 0; i < dict->capacity; i++) {
            if (dict->buckets[i].occupied == 1) {
                free((void *)dict->buckets[i].key);
            }
        }
    }
    free(dict->buckets);
    free(dict);
}

void mmc_dict_clear(MMC_Dict *dict)
{
    if (!dict) return;
    {
        size_t i;
        for (i = 0; i < dict->capacity; i++) {
            if (dict->buckets[i].occupied == 1) {
                free((void *)dict->buckets[i].key);
                dict->buckets[i].occupied = 0;
            } else if (dict->buckets[i].occupied == MMC_DICT_TOMBSTONE) {
                dict->buckets[i].occupied = 0;
            }
        }
    }
    dict->count = 0;
}

/* Internal: find the slot for a key.
 * Returns the bucket index.  Sets *tombstone_out to the first tombstone
 * slot encountered (if any), for use during insert. */
static size_t mmc_dict_find_slot(const MMC_Dict *dict, const char *key,
                                  size_t *tombstone_out)
{
    unsigned long h = mmc_djb2_hash(key);
    size_t idx = (size_t)(h & (dict->capacity - 1));
    size_t first_tombstone = dict->capacity;  /* sentinel: not found */

    while (1) {
        MMC_Dict_Entry *e = &dict->buckets[idx];
        if (e->occupied == 0) {
            /* Empty slot - key not in table */
            if (tombstone_out) *tombstone_out = first_tombstone;
            return idx;
        }
        if (e->occupied == MMC_DICT_TOMBSTONE) {
            /* Remember first tombstone for insertion reuse */
            if (first_tombstone == dict->capacity) {
                first_tombstone = idx;
            }
        } else if (strcmp(e->key, key) == 0) {
            /* Found the key */
            if (tombstone_out) *tombstone_out = dict->capacity;
            return idx;
        }
        idx = (idx + 1) & (dict->capacity - 1);
    }
}

MMC_Value mmc_dict_get(MMC_Dict *dict, const char *key)
{
    MMC_Value none = mmc_none();
    if (!dict || !key) { mmc_set_last_error(MMC_ERR_KEY); return none; }

    size_t slot = mmc_dict_find_slot(dict, key, NULL);
    MMC_Dict_Entry *e = &dict->buckets[slot];

    if (e->occupied == 1 && strcmp(e->key, key) == 0) {
        return e->value;
    }

    mmc_set_last_error(MMC_ERR_KEY);
    return none;
}

MMC_Value mmc_dict_get_default(MMC_Dict *dict, const char *key,
                                MMC_Value default_val)
{
    if (!dict || !key) return default_val;

    size_t slot = mmc_dict_find_slot(dict, key, NULL);
    MMC_Dict_Entry *e = &dict->buckets[slot];

    if (e->occupied == 1 && strcmp(e->key, key) == 0) {
        return e->value;
    }

    return default_val;
}

int mmc_dict_set(MMC_Dict *dict, const char *key, MMC_Value val)
{
    if (!dict || !key) return MMC_ERR_KEY;

    /* Resize if needed */
    if (mmc_dict_maybe_resize(dict) != MMC_OK) return MMC_ERR_MEMORY;

    size_t tombstone = dict->capacity;
    size_t slot = mmc_dict_find_slot(dict, key, &tombstone);
    MMC_Dict_Entry *e = &dict->buckets[slot];

    if (e->occupied == 1 && strcmp(e->key, key) == 0) {
        /* Key exists - update value */
        e->value = val;
        return MMC_OK;
    }

    /* Insert into tombstone slot if available, otherwise into empty slot */
    size_t insert_idx = (tombstone < dict->capacity) ? tombstone : slot;
    MMC_Dict_Entry *ie = &dict->buckets[insert_idx];

    ie->key = strdup(key);
    if (!ie->key) return MMC_ERR_MEMORY;
    ie->value = val;
    ie->occupied = 1;
    dict->count++;

    return MMC_OK;
}

int mmc_dict_delete(MMC_Dict *dict, const char *key)
{
    if (!dict || !key) return MMC_ERR_KEY;

    size_t slot = mmc_dict_find_slot(dict, key, NULL);
    MMC_Dict_Entry *e = &dict->buckets[slot];

    if (e->occupied != 1 || strcmp(e->key, key) != 0) {
        mmc_set_last_error(MMC_ERR_KEY);
        return MMC_ERR_KEY;
    }

    /* Free key and mark as tombstone */
    free((void *)e->key);
    e->key = NULL;
    e->occupied = MMC_DICT_TOMBSTONE;
    dict->count--;

    return MMC_OK;
}

int mmc_dict_has_key(MMC_Dict *dict, const char *key)
{
    if (!dict || !key) return 0;

    size_t slot = mmc_dict_find_slot(dict, key, NULL);
    MMC_Dict_Entry *e = &dict->buckets[slot];

    return (e->occupied == 1 && strcmp(e->key, key) == 0) ? 1 : 0;
}

size_t mmc_dict_len(MMC_Dict *dict)
{
    return dict ? dict->count : 0;
}

int mmc_dict_is_empty(MMC_Dict *dict)
{
    return dict ? (dict->count == 0) : 1;
}

MMC_Array *mmc_dict_keys(MMC_Dict *dict)
{
    MMC_Array *arr = mmc_arr_new();
    if (!dict) return arr;
    {
        size_t i;
        for (i = 0; i < dict->capacity; i++) {
            if (dict->buckets[i].occupied == 1) {
                mmc_arr_push(arr, mmc_string(dict->buckets[i].key));
            }
        }
    }
    return arr;
}

MMC_Array *mmc_dict_values(MMC_Dict *dict)
{
    MMC_Array *arr = mmc_arr_new();
    if (!dict) return arr;
    {
        size_t i;
        for (i = 0; i < dict->capacity; i++) {
            if (dict->buckets[i].occupied == 1) {
                mmc_arr_push(arr, dict->buckets[i].value);
            }
        }
    }
    return arr;
}


/* ============================================================================
 *  Section 5: String Operations (Myanmar Unicode Aware)
 * ============================================================================ */

/* ---- Basic Operations ---- */

size_t mmc_str_len(const char *s)
{
    return s ? strlen(s) : 0;
}

size_t mmc_str_char_len(const char *s)
{
    return s ? mmc_utf8_count(s) : 0;
}

char *mmc_str_dup(const char *s)
{
    if (!s) return NULL;
    MMC_Arena *arena = mmc_get_global_arena();
    if (arena) {
        return mmc_arena_strdup(arena, s);
    }
    return strdup(s);
}

char *mmc_str_concat(const char *a, const char *b)
{
    size_t la = a ? strlen(a) : 0;
    size_t lb = b ? strlen(b) : 0;
    size_t total = la + lb + 1;
    char *buf = mmc_result_buf(total);
    if (la > 0) memcpy(buf, a, la);
    if (lb > 0) memcpy(buf + la, b, lb);
    buf[la + lb] = '\0';
    return buf;
}

char *mmc_str_sub(const char *s, long start, long end)
{
    if (!s) s = "";
    long char_count = (long)mmc_utf8_count(s);

    /* Handle negative indices */
    if (start < 0) start += char_count;
    if (end < 0) end += char_count;
    if (start < 0) start = 0;
    if (end > char_count) end = char_count;
    if (start >= end) {
        char *buf = mmc_result_buf(1);
        buf[0] = '\0';
        return buf;
    }

    const char *p_start = mmc_utf8_advance(s, start);
    const char *p_end   = mmc_utf8_advance(p_start, end - start);

    size_t len = (size_t)(p_end - p_start);
    char *buf = mmc_result_buf(len + 1);
    memcpy(buf, p_start, len);
    buf[len] = '\0';
    return buf;
}

/* ---- Case Conversion (ASCII only, Myanmar unchanged) ---- */

char *mmc_str_upper(const char *s)
{
    if (!s) s = "";
    size_t len = strlen(s);
    char *buf = mmc_result_buf(len + 1);
    {
        size_t i;
        for (i = 0; i < len; i++) {
            /* Only uppercase ASCII letters; leave Myanmar/other unchanged */
            if ((unsigned char)s[i] < 0x80) {
                buf[i] = (char)toupper((unsigned char)s[i]);
            } else {
                /* Multi-byte UTF-8: copy all bytes of the character */
                unsigned char c = (unsigned char)s[i];
                int clen;
                if (c < 0xE0) clen = 2;
                else if (c < 0xF0) clen = 3;
                else clen = 4;
                if (i + clen <= len) {
                    memcpy(buf + i, s + i, (size_t)clen);
                    i += clen - 1;  /* -1 because loop does i++ */
                } else {
                    buf[i] = s[i];
                }
            }
        }
    }
    buf[len] = '\0';
    return buf;
}

char *mmc_str_lower(const char *s)
{
    if (!s) s = "";
    size_t len = strlen(s);
    char *buf = mmc_result_buf(len + 1);
    {
        size_t i;
        for (i = 0; i < len; i++) {
            if ((unsigned char)s[i] < 0x80) {
                buf[i] = (char)tolower((unsigned char)s[i]);
            } else {
                unsigned char c = (unsigned char)s[i];
                int clen;
                if (c < 0xE0) clen = 2;
                else if (c < 0xF0) clen = 3;
                else clen = 4;
                if (i + clen <= len) {
                    memcpy(buf + i, s + i, (size_t)clen);
                    i += clen - 1;
                } else {
                    buf[i] = s[i];
                }
            }
        }
    }
    buf[len] = '\0';
    return buf;
}

char *mmc_str_title(const char *s)
{
    if (!s) s = "";
    size_t len = strlen(s);
    char *buf = mmc_result_buf(len + 1);
    int new_word = 1;
    {
        size_t i;
        for (i = 0; i < len; i++) {
            unsigned char c = (unsigned char)s[i];
            if (c < 0x80) {
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                    buf[i] = s[i];
                    new_word = 1;
                } else if (new_word) {
                    buf[i] = (char)toupper(c);
                    new_word = 0;
                } else {
                    buf[i] = (char)tolower(c);
                }
            } else {
                /* Non-ASCII: copy the full character, treat as word content */
                int clen;
                if (c < 0xE0) clen = 2;
                else if (c < 0xF0) clen = 3;
                else clen = 4;
                if (i + clen <= len) {
                    memcpy(buf + i, s + i, (size_t)clen);
                    i += clen - 1;
                } else {
                    buf[i] = s[i];
                }
                new_word = 0;
            }
        }
    }
    buf[len] = '\0';
    return buf;
}

/* ---- Trimming ---- */

char *mmc_str_strip(const char *s)
{
    if (!s) s = "";
    while (*s && isspace((unsigned char)*s)) s++;
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) len--;
    char *buf = mmc_result_buf(len + 1);
    memcpy(buf, s, len);
    buf[len] = '\0';
    return buf;
}

char *mmc_str_lstrip(const char *s)
{
    if (!s) s = "";
    while (*s && isspace((unsigned char)*s)) s++;
    size_t len = strlen(s);
    char *buf = mmc_result_buf(len + 1);
    memcpy(buf, s, len);
    buf[len] = '\0';
    return buf;
}

char *mmc_str_rstrip(const char *s)
{
    if (!s) s = "";
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) len--;
    char *buf = mmc_result_buf(len + 1);
    memcpy(buf, s, len);
    buf[len] = '\0';
    return buf;
}

/* ---- Searching ---- */

long mmc_str_find(const char *haystack, const char *needle)
{
    if (!haystack || !needle || !*needle) return -1;
    const char *p = strstr(haystack, needle);
    if (!p) return -1;
    return (long)(p - haystack);
}

long mmc_str_rfind(const char *haystack, const char *needle)
{
    if (!haystack || !needle || !*needle) return -1;
    size_t hlen = strlen(haystack);
    size_t nlen = strlen(needle);
    if (nlen > hlen) return -1;

    /* Search from right */
    long pos = (long)(hlen - nlen);
    while (pos >= 0) {
        if (strncmp(haystack + (size_t)pos, needle, nlen) == 0)
            return pos;
        pos--;
    }
    return -1;
}

int mmc_str_starts_with(const char *s, const char *prefix)
{
    if (!s || !prefix) return 0;
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

int mmc_str_ends_with(const char *s, const char *suffix)
{
    if (!s || !suffix) return 0;
    size_t slen = strlen(s);
    size_t xlen = strlen(suffix);
    if (xlen > slen) return 0;
    return strcmp(s + slen - xlen, suffix) == 0;
}

int mmc_str_contains(const char *haystack, const char *needle)
{
    if (!haystack || !needle || !*needle) return 0;
    return strstr(haystack, needle) != NULL;
}

int mmc_str_count(const char *haystack, const char *needle)
{
    if (!haystack || !needle || !*needle) return 0;
    int count = 0;
    size_t nlen = strlen(needle);
    const char *p = haystack;
    while ((p = strstr(p, needle)) != NULL) {
        count++;
        p += nlen;  /* Non-overlapping */
    }
    return count;
}

/* ---- Modification ---- */

char *mmc_str_replace(const char *s, const char *old, const char *new_s)
{
    if (!s) s = "";
    if (!old || !*old) return mmc_str_dup(s);

    const char *p = strstr(s, old);
    if (!p) return mmc_str_dup(s);

    size_t prefix_len = (size_t)(p - s);
    size_t old_len = strlen(old);
    size_t new_len = new_s ? strlen(new_s) : 0;
    size_t suffix_len = strlen(p + old_len);
    size_t total = prefix_len + new_len + suffix_len + 1;

    char *buf = mmc_result_buf(total);
    if (prefix_len > 0) memcpy(buf, s, prefix_len);
    if (new_len > 0)   memcpy(buf + prefix_len, new_s, new_len);
    memcpy(buf + prefix_len + new_len, p + old_len, suffix_len + 1);
    return buf;
}

char *mmc_str_replace_all(const char *s, const char *old, const char *new_s)
{
    if (!s) s = "";
    if (!old || !*old) return mmc_str_dup(s);

    size_t old_len = strlen(old);
    size_t new_len = new_s ? strlen(new_s) : 0;
    size_t s_len = strlen(s);

    /* First pass: count occurrences */
    int count = mmc_str_count(s, old);
    if (count == 0) return mmc_str_dup(s);

    /* Calculate result size */
    size_t total = s_len + (size_t)count * new_len
                        - (size_t)count * old_len + 1;
    char *buf = mmc_result_buf(total);

    const char *src = s;
    char *dst = buf;

    while (*src) {
        const char *p = strstr(src, old);
        if (!p) {
            /* Copy remainder */
            size_t rem = strlen(src);
            memcpy(dst, src, rem);
            dst += rem;
            break;
        }
        /* Copy text before match */
        size_t before = (size_t)(p - src);
        if (before > 0) {
            memcpy(dst, src, before);
            dst += before;
        }
        /* Copy replacement */
        if (new_len > 0) {
            memcpy(dst, new_s, new_len);
            dst += new_len;
        }
        src = p + old_len;
    }

    *dst = '\0';
    return buf;
}

/* ---- Split / Join ---- */

MMC_Array *mmc_str_split(const char *s, const char *delim)
{
    MMC_Array *arr = mmc_arr_new();
    if (!s) return arr;

    MMC_Arena *arena = mmc_get_global_arena();

    if (!delim || !*delim) {
        /* Split each character (Unicode-aware) */
        const char *p = s;
        while (*p) {
            const char *start = p;
            unsigned char c = (unsigned char)*p;
            if (c < 0x80)       p += 1;
            else if (c < 0xE0)  p += 2;
            else if (c < 0xF0)  p += 3;
            else                 p += 4;

            size_t clen = (size_t)(p - start);
            char *piece;
            if (arena) {
                piece = (char *)mmc_arena_alloc(arena, clen + 1);
            } else {
                piece = (char *)malloc(clen + 1);
            }
            if (piece) {
                memcpy(piece, start, clen);
                piece[clen] = '\0';
            }
            mmc_arr_push(arr, mmc_string(piece ? piece : ""));
        }
    } else {
        /* Split by delimiter */
        size_t dlen = strlen(delim);
        const char *p = s;
        while (*p) {
            const char *next = strstr(p, delim);
            if (!next) {
                /* Last piece */
                char *piece;
                if (arena) {
                    piece = mmc_arena_strdup(arena, p);
                } else {
                    piece = strdup(p);
                }
                mmc_arr_push(arr, mmc_string(piece ? piece : ""));
                break;
            }
            size_t plen = (size_t)(next - p);
            char *piece;
            if (arena) {
                piece = (char *)mmc_arena_alloc(arena, plen + 1);
            } else {
                piece = (char *)malloc(plen + 1);
            }
            if (piece) {
                memcpy(piece, p, plen);
                piece[plen] = '\0';
            }
            mmc_arr_push(arr, mmc_string(piece ? piece : ""));
            p = next + dlen;
        }
    }

    return arr;
}

MMC_Array *mmc_str_split_lines(const char *s)
{
    MMC_Array *arr = mmc_arr_new();
    if (!s) return arr;

    MMC_Arena *arena = mmc_get_global_arena();
    const char *p = s;

    while (*p) {
        const char *line_start = p;
        /* Find end of line: \n, \r\n, or \r */
        while (*p && *p != '\n' && *p != '\r') p++;

        size_t len = (size_t)(p - line_start);
        char *line;
        if (arena) {
            line = (char *)mmc_arena_alloc(arena, len + 1);
        } else {
            line = (char *)malloc(len + 1);
        }
        if (line) {
            memcpy(line, line_start, len);
            line[len] = '\0';
        }
        mmc_arr_push(arr, mmc_string(line ? line : ""));

        /* Skip line terminator(s) */
        if (*p == '\r') p++;
        if (*p == '\n') p++;
    }

    return arr;
}

char *mmc_str_join(MMC_Array *arr, const char *delim)
{
    if (!arr || arr->len == 0) {
        char *buf = mmc_result_buf(1);
        buf[0] = '\0';
        return buf;
    }

    size_t dlen = delim ? strlen(delim) : 0;

    /* Calculate total size */
    size_t total = 1;  /* null terminator */
    {
        size_t i;
        for (i = 0; i < arr->len; i++) {
            const char *s = mmc_value_to_str(arr->data[i]);
            total += strlen(s);
            if (i > 0) total += dlen;
        }
    }

    char *buf = mmc_result_buf(total);
    size_t pos = 0;
    {
        size_t i;
        for (i = 0; i < arr->len; i++) {
            if (i > 0 && dlen > 0) {
                memcpy(buf + pos, delim, dlen);
                pos += dlen;
            }
            const char *s = mmc_value_to_str(arr->data[i]);
            size_t slen = strlen(s);
            memcpy(buf + pos, s, slen);
            pos += slen;
        }
    }
    buf[pos] = '\0';
    return buf;
}

/* ---- Character Classification (Unicode-aware) ---- */

/* Check if a codepoint is a Myanmar Unicode character (U+1000-U+109F) */
int mmc_char_is_myanmar(int codepoint)
{
    return (codepoint >= 0x1000 && codepoint <= 0x109F);
}

int mmc_str_isalpha(const char *s)
{
    if (!s || !*s) return 0;
    const char *p = s;
    while (*p) {
        int cp = mmc_utf8_decode(&p);
        /* ASCII letter */
        if (cp >= 'A' && cp <= 'Z') continue;
        if (cp >= 'a' && cp <= 'z') continue;
        /* Myanmar letter (U+1000-U+109F excluding digits U+1040-U+1049
         * and some punctuation U+104A-U+104F) */
        if (mmc_char_is_myanmar(cp) &&
            !(cp >= 0x1040 && cp <= 0x104F)) continue;
        return 0;
    }
    return 1;
}

int mmc_str_isdigit(const char *s)
{
    if (!s || !*s) return 0;
    const char *p = s;
    while (*p) {
        int cp = mmc_utf8_decode(&p);
        /* ASCII digit */
        if (cp >= '0' && cp <= '9') continue;
        /* Myanmar digit (U+1040-U+1049) */
        if (cp >= 0x1040 && cp <= 0x1049) continue;
        return 0;
    }
    return 1;
}

int mmc_str_isalnum(const char *s)
{
    if (!s || !*s) return 0;
    const char *p = s;
    while (*p) {
        int cp = mmc_utf8_decode(&p);
        if (cp >= 'A' && cp <= 'Z') continue;
        if (cp >= 'a' && cp <= 'z') continue;
        if (cp >= '0' && cp <= '9') continue;
        if (mmc_char_is_myanmar(cp)) continue;
        return 0;
    }
    return 1;
}

int mmc_str_isspace(const char *s)
{
    if (!s || !*s) return 0;
    const char *p = s;
    while (*p) {
        int cp = mmc_utf8_decode(&p);
        if (cp == ' ' || cp == '\t' || cp == '\n' ||
            cp == '\r' || cp == '\f' || cp == '\v') continue;
        return 0;
    }
    return 1;
}

int mmc_str_isupper(const char *s)
{
    if (!s || !*s) return 0;
    /* At least one ASCII uppercase letter, no ASCII lowercase letters */
    int has_upper = 0;
    const char *p = s;
    while (*p) {
        int cp = mmc_utf8_decode(&p);
        if (cp >= 'a' && cp <= 'z') return 0;
        if (cp >= 'A' && cp <= 'Z') has_upper = 1;
        /* Non-ASCII characters don't affect result */
    }
    return has_upper;
}

int mmc_str_islower(const char *s)
{
    if (!s || !*s) return 0;
    int has_lower = 0;
    const char *p = s;
    while (*p) {
        int cp = mmc_utf8_decode(&p);
        if (cp >= 'A' && cp <= 'Z') return 0;
        if (cp >= 'a' && cp <= 'z') has_lower = 1;
    }
    return has_lower;
}

/* ---- Myanmar-Specific Functions ---- */

int mmc_str_is_myanmar(const char *s)
{
    if (!s || !*s) return 0;
    const char *p = s;
    while (*p) {
        int cp = mmc_utf8_decode(&p);
        if (mmc_char_is_myanmar(cp)) return 1;
        /* Skip whitespace and ASCII */
    }
    return 0;
}

char *mmc_str_myanmar_digits_to_latin(const char *s)
{
    if (!s) s = "";
    size_t len = strlen(s);
    char *buf = mmc_result_buf(len + 1);
    {
        size_t i = 0, j = 0;
        while (i < len) {
            unsigned char c = (unsigned char)s[i];
            /* Check for 3-byte Myanmar digit (E1 81 80 to E1 81 89) */
            if (i + 2 < len &&
                c == 0xE1 &&
                (unsigned char)s[i + 1] == 0x81 &&
                (unsigned char)s[i + 2] >= 0x80 &&
                (unsigned char)s[i + 2] <= 0x89) {
                buf[j++] = (char)('0' + ((unsigned char)s[i + 2] - 0x80));
                i += 3;
            } else {
                buf[j++] = s[i++];
            }
        }
        buf[j] = '\0';
    }
    return buf;
}

char *mmc_str_latin_digits_to_myanmar(const char *s)
{
    if (!s) s = "";
    size_t len = strlen(s);
    /* Worst case: every ASCII digit becomes 3 bytes */
    size_t max_out = len * 3 + 1;
    char *buf = mmc_result_buf(max_out);
    {
        size_t i = 0, j = 0;
        while (i < len) {
            if (s[i] >= '0' && s[i] <= '9') {
                /* Encode Myanmar digit: U+1040 + digit value */
                int cp = 0x1040 + (int)(s[i] - '0');
                j += (size_t)mmc_utf8_encode(buf + j, cp);
                i++;
            } else {
                buf[j++] = s[i++];
            }
        }
        buf[j] = '\0';
    }
    return buf;
}


/* ============================================================================
 *  Section 7: Print and Format Functions
 * ============================================================================ */

void mmc_print(int argc, MMC_Value *argv)
{
    mmc_print_sep(" ", argc, argv);
}

void mmc_print_sep(const char *sep, int argc, MMC_Value *argv)
{
    int i;
    for (i = 0; i < argc; i++) {
        if (i > 0 && sep) printf("%s", sep);
        const char *s = mmc_value_to_str(argv[i]);
        printf("%s", s);
    }
    printf("\n");
    fflush(stdout);
}

void mmc_print_no_nl(int argc, MMC_Value *argv)
{
    int i;
    for (i = 0; i < argc; i++) {
        if (i > 0) printf(" ");
        const char *s = mmc_value_to_str(argv[i]);
        printf("%s", s);
    }
    fflush(stdout);
}

/* ---- Formatted Print (printf-like with MMC_Value) ---- *
 * Supports format specifiers:
 *   %d, %i  - integer (MMC_Value must be INT)
 *   %f      - float
 *   %s      - string
 *   %b      - boolean ("true"/"false")
 *   %v      - any MMC_Value (auto-formatted)
 *   %lld    - long long (via va_arg directly)
 *   %lf     - double (via va_arg directly)
 *   %%      - literal percent
 */
void mmc_printf(const char *fmt, ...)
{
    if (!fmt) return;
    va_list ap;
    va_start(ap, fmt);

    const char *p = fmt;
    while (*p) {
        if (*p != '%') {
            putchar(*p++);
            continue;
        }
        p++;  /* skip '%' */

        if (!*p) break;

        switch (*p) {
            case 'd': case 'i': {
                /* Expect MMC_Value* (pointer) via va_arg */
                MMC_Value v = va_arg(ap, MMC_Value);
                printf("%lld", mmc_as_int(v));
                break;
            }
            case 'f': {
                MMC_Value v = va_arg(ap, MMC_Value);
                printf("%f", mmc_as_float(v));
                break;
            }
            case 'g': {
                MMC_Value v = va_arg(ap, MMC_Value);
                printf("%g", mmc_as_float(v));
                break;
            }
            case 's': {
                const char *s = va_arg(ap, const char *);
                printf("%s", s ? s : "");
                break;
            }
            case 'b': {
                MMC_Value v = va_arg(ap, MMC_Value);
                printf("%s", mmc_as_bool(v) ? "true" : "false");
                break;
            }
            case 'v': {
                MMC_Value v = va_arg(ap, MMC_Value);
                const char *s = mmc_value_to_str(v);
                printf("%s", s);
                break;
            }
            case 'l': {
                /* %lld or %lf */
                if (*(p + 1) == 'l' && *(p + 2) == 'd') {
                    long long val = va_arg(ap, long long);
                    printf("%lld", val);
                    p += 2;
                } else if (*(p + 1) == 'f') {
                    double val = va_arg(ap, double);
                    printf("%lf", val);
                    p += 1;
                } else {
                    putchar('%');
                    putchar('l');
                }
                break;
            }
            case '%':
                putchar('%');
                break;
            default:
                putchar('%');
                putchar(*p);
                break;
        }
        p++;
    }

    va_end(ap);
    fflush(stdout);
}

/* ---- Input ---- */

static char mmc_input_buf[8192];

const char *mmc_input(const char *prompt)
{
    if (prompt) {
        printf("%s", prompt);
        fflush(stdout);
    }
    if (fgets(mmc_input_buf, (int)sizeof(mmc_input_buf), stdin) != NULL) {
        /* Strip trailing newline/carriage-return */
        size_t len = strlen(mmc_input_buf);
        while (len > 0 && (mmc_input_buf[len - 1] == '\n' ||
                           mmc_input_buf[len - 1] == '\r')) {
            mmc_input_buf[--len] = '\0';
        }
        return mmc_input_buf;
    }
    mmc_input_buf[0] = '\0';
    return mmc_input_buf;
}

MMC_Value mmc_input_value(const char *prompt)
{
    const char *line = mmc_input(prompt);
    if (!line || !*line) return mmc_string("");

    /* Try to parse as integer */
    char *endp;
    errno = 0;
    long long ival = strtoll(line, &endp, 10);
    if (*endp == '\0' && errno == 0) return mmc_int(ival);

    /* Try to parse as float */
    errno = 0;
    double fval = strtod(line, &endp);
    if (*endp == '\0' && errno == 0) return mmc_float(fval);

    /* Return as string */
    return mmc_string(mmc_str_dup(line));
}


/* ============================================================================
 *  Section 8: Range Iterator
 * ============================================================================ */

MMC_Range mmc_range(long long stop)
{
    MMC_Range r;
    r.current = 0;
    r.stop = stop;
    r.step = 1;
    r.done = (stop <= 0);
    return r;
}

MMC_Range mmc_range_start(long long start, long long stop)
{
    MMC_Range r;
    r.current = start;
    r.stop = stop;
    r.step = (start <= stop) ? 1 : -1;
    r.done = 0;
    return r;
}

MMC_Range mmc_range_step(long long start, long long stop, long long step)
{
    MMC_Range r;
    r.current = start;
    r.stop = stop;
    r.step = step;
    r.done = 0;
    return r;
}

long long mmc_range_next(MMC_Range *r)
{
    if (!r || r->done) return r ? r->stop : 0;
    long long val = r->current;
    r->current += r->step;
    /* Check completion */
    if (r->step > 0 && r->current >= r->stop) r->done = 1;
    else if (r->step < 0 && r->current <= r->stop) r->done = 1;
    else if (r->step == 0) r->done = 1;  /* infinite loop guard */
    return val;
}

int mmc_range_done(MMC_Range *r)
{
    return r ? r->done : 1;
}


/* ============================================================================
 *  Section 9: File I/O
 * ============================================================================ */

MMC_File *mmc_file_open(const char *path, const char *mode)
{
    if (!path || !mode) return NULL;

    MMC_File *f = (MMC_File *)malloc(sizeof(MMC_File));
    if (!f) return NULL;

    f->fp = fopen(path, mode);
    if (!f->fp) {
        free(f);
        return NULL;
    }

    mmc_safe_strcpy(f->path, path, sizeof(f->path));
    mmc_safe_strcpy(f->mode, mode, sizeof(f->mode));
    return f;
}

int mmc_file_close(MMC_File *f)
{
    if (!f) return MMC_ERR_IO;
    int ret = fclose(f->fp);
    free(f);
    return (ret == 0) ? MMC_OK : MMC_ERR_IO;
}

char *mmc_file_read_all(MMC_File *f)
{
    if (!f || !f->fp) return NULL;

    /* Seek to end to determine size */
    if (fseek(f->fp, 0, SEEK_END) != 0) return NULL;
    long size = ftell(f->fp);
    if (size < 0) size = 0;
    fseek(f->fp, 0, SEEK_SET);

    if (size == 0) {
        char *buf = mmc_result_buf(1);
        buf[0] = '\0';
        return buf;
    }

    char *buf = mmc_result_buf((size_t)size + 1);
    size_t nread = fread(buf, 1, (size_t)size, f->fp);
    buf[nread] = '\0';
    return buf;
}

MMC_Array *mmc_file_read_lines(MMC_File *f)
{
    MMC_Array *arr = mmc_arr_new();
    if (!f || !f->fp) return arr;

    MMC_Arena *arena = mmc_get_global_arena();
    char line_buf[8192];

    while (fgets(line_buf, (int)sizeof(line_buf), f->fp)) {
        /* Strip trailing newline/carriage-return */
        size_t len = strlen(line_buf);
        while (len > 0 && (line_buf[len - 1] == '\n' ||
                           line_buf[len - 1] == '\r')) {
            line_buf[--len] = '\0';
        }
        char *line;
        if (arena) {
            line = mmc_arena_strdup(arena, line_buf);
        } else {
            line = strdup(line_buf);
        }
        mmc_arr_push(arr, mmc_string(line ? line : ""));
    }

    return arr;
}

int mmc_file_write(MMC_File *f, const char *data)
{
    if (!f || !f->fp || !data) return MMC_ERR_IO;
    size_t len = strlen(data);
    size_t written = fwrite(data, 1, len, f->fp);
    return (written == len) ? MMC_OK : MMC_ERR_IO;
}

int mmc_file_write_line(MMC_File *f, const char *line)
{
    if (!f || !f->fp) return MMC_ERR_IO;
    if (line) {
        if (fputs(line, f->fp) == EOF) return MMC_ERR_IO;
    }
    if (fputc('\n', f->fp) == EOF) return MMC_ERR_IO;
    return MMC_OK;
}

long mmc_file_tell(MMC_File *f)
{
    if (!f || !f->fp) return -1;
    return ftell(f->fp);
}

int mmc_file_seek(MMC_File *f, long offset, int whence)
{
    if (!f || !f->fp) return MMC_ERR_IO;
    return (fseek(f->fp, offset, whence) == 0) ? MMC_OK : MMC_ERR_IO;
}

int mmc_file_exists(const char *path)
{
    if (!path) return 0;
#ifdef _WIN32
    return (_access(path, 0) == 0);
#else
    return (access(path, F_OK) == 0);
#endif
}

long mmc_file_size(const char *path)
{
    if (!path) return -1;
#ifdef _WIN32
    struct _stat st;
    if (_stat(path, &st) != 0) return -1;
    return (long)st.st_size;
#else
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (long)st.st_size;
#endif
}


/* ============================================================================
 *  Section 10: System and Platform
 * ============================================================================ */

const char *mmc_platform_name(void)
{
#if defined(_WIN32) || defined(_WIN64)
    return "Windows";
#elif defined(__ANDROID__)
    return "Android";
#elif defined(__APPLE__) && TARGET_OS_IPHONE
    return "iOS";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    return "Linux";
#elif defined(__FreeBSD__)
    return "FreeBSD";
#elif defined(__OpenBSD__)
    return "OpenBSD";
#elif defined(__NetBSD__)
    return "NetBSD";
#else
    return "Unknown";
#endif
}

const char *mmc_arch_name(void)
{
#if defined(__x86_64__) || defined(_M_X64) || defined(__amd64__)
    return "x86_64";
#elif defined(__i386__) || defined(_M_IX86) || defined(__i486__) || defined(__i586__) || defined(__i686__)
    return "x86";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__arm__) || defined(_M_ARM)
    return "arm";
#elif defined(__riscv) || defined(__riscv__)
    return "riscv";
#elif defined(__mips__) || defined(__mips)
    return "mips";
#elif defined(__powerpc__) || defined(__powerpc64__)
    return "ppc";
#elif defined(__s390x__) || defined(__s390__)
    return "s390";
#else
    return "unknown";
#endif
}

long long mmc_time_ms(void)
{
#ifdef _WIN32
    FILETIME ft;
    ULARGE_INTEGER ul;
    GetSystemTimeAsFileTime(&ft);
    ul.LowPart  = ft.dwLowDateTime;
    ul.HighPart = ft.dwHighDateTime;
    /* Convert 100-nanosecond intervals since 1601-01-01 to ms since epoch */
    return (long long)(ul.QuadPart / 10000ULL - 11644473600000ULL);
#elif defined(__APPLE__) && !TARGET_OS_IPHONE
    /* macOS: clock_gettime is available but let's use gettimeofday */
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        return (long long)tv.tv_sec * 1000LL + tv.tv_usec / 1000;
    }
#else
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        return (long long)tv.tv_sec * 1000LL + tv.tv_usec / 1000;
    }
#endif
}

long long mmc_time_sec(void)
{
#ifdef _WIN32
    return mmc_time_ms() / 1000LL;
#elif defined(CLOCK_REALTIME)
    {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        return (long long)ts.tv_sec;
    }
#else
    return (long long)time(NULL);
#endif
}

void mmc_sleep_ms(long long ms)
{
    if (ms <= 0) return;
#ifdef _WIN32
    Sleep((DWORD)(ms > (long long)UINT32_MAX ? UINT32_MAX : (unsigned int)ms));
#else
    {
        struct timespec ts;
        ts.tv_sec  = (time_t)(ms / 1000);
        ts.tv_nsec = (long)((ms % 1000) * 1000000L);
        nanosleep(&ts, NULL);
    }
#endif
}

void mmc_sleep_sec(double sec)
{
    if (sec <= 0.0) return;
    mmc_sleep_ms((long long)(sec * 1000.0 + 0.5));
}

const char *mmc_getenv(const char *name)
{
    if (!name) return NULL;
    return getenv(name);
}

int mmc_setenv(const char *name, const char *value)
{
    if (!name || !value) return MMC_ERR_RUNTIME;
#ifdef _WIN32
    return (_putenv_s(name, value) == 0) ? MMC_OK : MMC_ERR_RUNTIME;
#else
    return (setenv(name, value, 1) == 0) ? MMC_OK : MMC_ERR_RUNTIME;
#endif
}


/* ============================================================================
 *  Section 11: Error Handling
 * ============================================================================ */

const char *mmc_error_name(MMC_Error err)
{
    switch (err) {
        case MMC_OK:           return "MMC_OK";
        case MMC_ERR_MEMORY:   return "MMC_ERR_MEMORY";
        case MMC_ERR_INDEX:    return "MMC_ERR_INDEX";
        case MMC_ERR_TYPE:     return "MMC_ERR_TYPE";
        case MMC_ERR_KEY:      return "MMC_ERR_KEY";
        case MMC_ERR_IO:       return "MMC_ERR_IO";
        case MMC_ERR_OVERFLOW: return "MMC_ERR_OVERFLOW";
        case MMC_ERR_DIV_ZERO: return "MMC_ERR_DIV_ZERO";
        case MMC_ERR_RUNTIME:  return "MMC_ERR_RUNTIME";
        default:               return "MMC_ERR_UNKNOWN";
    }
}

const char *mmc_error_msg(MMC_Error err)
{
    switch (err) {
        case MMC_OK:           return "No error";
        case MMC_ERR_MEMORY:   return "Memory allocation failed";
        case MMC_ERR_INDEX:    return "Index out of range";
        case MMC_ERR_TYPE:     return "Type mismatch";
        case MMC_ERR_KEY:      return "Key not found";
        case MMC_ERR_IO:       return "Input/output error";
        case MMC_ERR_OVERFLOW: return "Numeric overflow";
        case MMC_ERR_DIV_ZERO: return "Division by zero";
        case MMC_ERR_RUNTIME:  return "Runtime error";
        default:               return "Unknown error";
    }
}

void mmc_set_last_error(MMC_Error err)
{
    g_mmc_last_error = err;
}

MMC_Error mmc_get_last_error(void)
{
    return g_mmc_last_error;
}


/* ============================================================================
 *  Section 12: MMC Runtime Initialization
 * ============================================================================ */

MMC_Error mmc_runtime_init(void)
{
    /* Create the global arena */
    if (g_mmc_arena) {
        /* Already initialised - reset and reuse */
        mmc_arena_reset(g_mmc_arena);
    } else {
        g_mmc_arena = mmc_arena_new();
        if (!g_mmc_arena) return MMC_ERR_MEMORY;
    }

    /* Reset error state */
    g_mmc_last_error = MMC_OK;

    /* Reset static buffer rotation */
    mmc_static_buf_next = 0;

    return MMC_OK;
}

MMC_Error mmc_runtime_cleanup(void)
{
    /* Free the global arena and all its memory */
    if (g_mmc_arena) {
        mmc_arena_free(g_mmc_arena);
        g_mmc_arena = NULL;
    }

    /* Reset error state */
    g_mmc_last_error = MMC_OK;

    return MMC_OK;
}


/* ============================================================================
 *  Self-Test Entry Point (Optional)
 *
 *  Compile with -DMMC_RUNTIME_SELFTEST to build a standalone executable
 *  that exercises all major runtime functionality.
 *
 *  Example:
 *      gcc -std=c99 -Wall -Wextra -DMMC_RUNTIME_SELFTEST \
 *          mmclib.c -o mmclib_test && ./mmclib_test
 * ============================================================================ */

#ifdef MMC_RUNTIME_SELFTEST

static int selftest_pass = 0;
static int selftest_fail = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (cond) { selftest_pass++; } \
    else { selftest_fail++; printf("  FAIL: %s\n", msg); } \
} while(0)

static void test_section2_values(void)
{
    printf("  Section 2: MMC Values...\n");

    MMC_Value v_int = mmc_int(42);
    TEST_ASSERT(v_int.type == MMC_TYPE_INT, "int type");
    TEST_ASSERT(v_int.int_val == 42, "int value");

    MMC_Value v_float = mmc_float(3.14);
    TEST_ASSERT(v_float.type == MMC_TYPE_FLOAT, "float type");
    TEST_ASSERT(mmc_as_int(v_float) == 3, "float to int");

    MMC_Value v_str = mmc_string("hello");
    TEST_ASSERT(mmc_is_string(v_str), "is string");
    TEST_ASSERT(strcmp(v_str.str_val, "hello") == 0, "string value");

    MMC_Value v_bool = mmc_bool(1);
    TEST_ASSERT(mmc_is_bool(v_bool) && v_bool.bool_val, "bool true");

    MMC_Value v_none = mmc_none();
    TEST_ASSERT(mmc_is_none(v_none), "is none");

    /* Equality */
    TEST_ASSERT(mmc_values_equal(mmc_int(5), mmc_float(5.0)), "int==float");
    TEST_ASSERT(!mmc_values_equal(mmc_int(5), mmc_int(6)), "int!=int");

    /* Type name */
    TEST_ASSERT(strcmp(mmc_type_name(v_int), "int") == 0, "type name int");

    /* value_to_str */
    MMC_Value v_f2 = mmc_float(3.0);
    TEST_ASSERT(strcmp(mmc_value_to_str(v_f2), "3.0") == 0, "float 3.0 str");
}

static void test_section3_array(void)
{
    printf("  Section 3: Arrays...\n");

    MMC_Array *arr = mmc_arr_new();
    TEST_ASSERT(arr != NULL, "array new");
    TEST_ASSERT(mmc_arr_len(arr) == 0, "array empty");
    TEST_ASSERT(mmc_arr_is_empty(arr), "array is_empty");

    mmc_arr_push(arr, mmc_int(10));
    mmc_arr_push(arr, mmc_int(20));
    mmc_arr_push(arr, mmc_int(30));
    TEST_ASSERT(mmc_arr_len(arr) == 3, "push 3");
    TEST_ASSERT(mmc_arr_get(arr, 1).int_val == 20, "get index 1");
    TEST_ASSERT(mmc_arr_front(arr).int_val == 10, "front");
    TEST_ASSERT(mmc_arr_back(arr).int_val == 30, "back");

    MMC_Value popped = mmc_arr_pop(arr);
    TEST_ASSERT(popped.int_val == 30, "pop");
    TEST_ASSERT(mmc_arr_len(arr) == 2, "after pop len");

    mmc_arr_insert(arr, 1, mmc_int(15));
    TEST_ASSERT(mmc_arr_get(arr, 1).int_val == 15, "insert");

    TEST_ASSERT(mmc_arr_contains(arr, mmc_int(20)), "contains");
    TEST_ASSERT(mmc_arr_index_of(arr, mmc_int(10)) == 0, "index_of");

    /* Reverse */
    mmc_arr_reverse(arr);
    TEST_ASSERT(mmc_arr_front(arr).int_val == 20, "reverse front");

    /* Slice */
    MMC_Array *s = mmc_arr_slice(arr, 0, 2);
    TEST_ASSERT(mmc_arr_len(s) == 2, "slice len");

    /* Sort */
    mmc_arr_push(arr, mmc_int(5));
    mmc_arr_sort_int(arr);
    TEST_ASSERT(mmc_arr_front(arr).int_val == 5, "sort front");

    mmc_arr_free(s);
    mmc_arr_free(arr);
}

static void test_section4_dict(void)
{
    printf("  Section 4: Dicts...\n");

    MMC_Dict *d = mmc_dict_new();
    TEST_ASSERT(d != NULL, "dict new");
    TEST_ASSERT(mmc_dict_is_empty(d), "dict empty");

    mmc_dict_set(d, "name", mmc_string("MMC"));
    mmc_dict_set(d, "version", mmc_int(2));
    TEST_ASSERT(mmc_dict_len(d) == 2, "dict len 2");
    TEST_ASSERT(mmc_dict_has_key(d, "name"), "has_key");

    MMC_Value v = mmc_dict_get(d, "name");
    TEST_ASSERT(strcmp(v.str_val, "MMC") == 0, "dict get name");

    MMC_Value def = mmc_dict_get_default(d, "missing", mmc_none());
    TEST_ASSERT(mmc_is_none(def), "dict get_default");

    mmc_dict_delete(d, "version");
    TEST_ASSERT(mmc_dict_len(d) == 1, "dict after delete");

    MMC_Array *keys = mmc_dict_keys(d);
    TEST_ASSERT(mmc_arr_len(keys) == 1, "dict keys");
    mmc_arr_free(keys);

    MMC_Array *vals = mmc_dict_values(d);
    TEST_ASSERT(mmc_arr_len(vals) == 1, "dict values");
    mmc_arr_free(vals);

    mmc_dict_free(d);
}

static void test_section5_strings(void)
{
    printf("  Section 5: Strings...\n");

    /* Basic */
    TEST_ASSERT(mmc_str_len("hello") == 5, "str_len");
    TEST_ASSERT(mmc_str_char_len("hello") == 5, "str_char_len");

    /* Concat */
    const char *cat = mmc_str_concat("foo", "bar");
    TEST_ASSERT(strcmp(cat, "foobar") == 0, "concat");

    /* Substring */
    const char *sub = mmc_str_sub("hello world", 0, 5);
    TEST_ASSERT(strcmp(sub, "hello") == 0, "sub");

    /* Case */
    const char *up = mmc_str_upper("hello");
    TEST_ASSERT(strcmp(up, "HELLO") == 0, "upper");
    const char *lo = mmc_str_lower("HELLO");
    TEST_ASSERT(strcmp(lo, "hello") == 0, "lower");

    /* Trim */
    const char *stripped = mmc_str_strip("  hi  ");
    TEST_ASSERT(strcmp(stripped, "hi") == 0, "strip");

    /* Find */
    TEST_ASSERT(mmc_str_find("hello world", "world") == 6, "find");
    TEST_ASSERT(mmc_str_contains("hello", "ell") == 1, "contains");
    TEST_ASSERT(mmc_str_starts_with("hello", "hel") == 1, "starts_with");
    TEST_ASSERT(mmc_str_ends_with("hello", "llo") == 1, "ends_with");

    /* Replace */
    const char *rep = mmc_str_replace("hello world", "world", "MMC");
    TEST_ASSERT(strcmp(rep, "hello MMC") == 0, "replace");

    const char *repall = mmc_str_replace_all("aabbcc", "b", "X");
    TEST_ASSERT(strcmp(repall, "aaXXcc") == 0, "replace_all");

    /* Split */
    MMC_Array *sp = mmc_str_split("a,b,c", ",");
    TEST_ASSERT(mmc_arr_len(sp) == 3, "split len");
    mmc_arr_free(sp);

    /* Join */
    MMC_Array *jarr = mmc_arr_new();
    mmc_arr_push(jarr, mmc_string("a"));
    mmc_arr_push(jarr, mmc_string("b"));
    const char *joined = mmc_str_join(jarr, "-");
    TEST_ASSERT(strcmp(joined, "a-b") == 0, "join");
    mmc_arr_free(jarr);

    /* Classification */
    TEST_ASSERT(mmc_str_isalpha("hello") == 1, "isalpha");
    TEST_ASSERT(mmc_str_isdigit("12345") == 1, "isdigit");
    TEST_ASSERT(mmc_str_isalnum("abc123") == 1, "isalnum");
    TEST_ASSERT(mmc_str_isupper("HELLO") == 1, "isupper");
    TEST_ASSERT(mmc_str_islower("hello") == 1, "islower");

    /* Myanmar digits */
    /* Myanmar digits U+1041(U+1041=၁) U+1042(၂) U+1043(၃) */
    const char *my = "\xE1\x81\x81\xE1\x81\x82\xE1\x81\x83";
    const char *lat = mmc_str_myanmar_digits_to_latin(my);
    TEST_ASSERT(strcmp(lat, "123") == 0, "myanmar to latin");

    const char *my2 = mmc_str_latin_digits_to_myanmar("123");
    TEST_ASSERT(strlen(my2) == 9, "latin to myanmar len");

    TEST_ASSERT(mmc_char_is_myanmar(0x1000) == 1, "char is myanmar");
    TEST_ASSERT(mmc_char_is_myanmar('A') == 0, "char not myanmar");
}

static void test_section6_arena(void)
{
    printf("  Section 6: Arena...\n");

    MMC_Arena *arena = mmc_arena_new();
    TEST_ASSERT(arena != NULL, "arena new");

    void *p1 = mmc_arena_alloc(arena, 100);
    void *p2 = mmc_arena_alloc(arena, 200);
    TEST_ASSERT(p1 != NULL && p2 != NULL, "arena alloc");
    TEST_ASSERT(p1 != p2, "arena alloc different");

    const char *s = mmc_arena_strdup(arena, "test string");
    TEST_ASSERT(strcmp(s, "test string") == 0, "arena strdup");

    mmc_arena_reset(arena);
    TEST_ASSERT(arena->total_allocated == 0, "arena reset");

    mmc_arena_free(arena);
}

static void test_section8_range(void)
{
    printf("  Section 8: Range...\n");

    MMC_Range r = mmc_range(5);
    int count = 0;
    while (!mmc_range_done(&r)) {
        long long val = mmc_range_next(&r);
        TEST_ASSERT(val == count, "range value");
        count++;
    }
    TEST_ASSERT(count == 5, "range count");

    /* Negative step: range(4, -1, -1) yields 4,3,2,1,0 = 5 values */
    MMC_Range r2 = mmc_range_step(4, -1, -1);
    count = 0;
    while (!mmc_range_done(&r2)) {
        mmc_range_next(&r2);
        count++;
    }
    TEST_ASSERT(count == 5, "range step count");
}

static void test_section9_file(void)
{
    printf("  Section 9: File I/O...\n");

    const char *tmpfile = "mmc_test_tmp.txt";

    MMC_File *f = mmc_file_open(tmpfile, "w");
    TEST_ASSERT(f != NULL, "file open write");

    mmc_file_write_line(f, "Hello");
    mmc_file_write_line(f, "World");
    mmc_file_close(f);

    TEST_ASSERT(mmc_file_exists(tmpfile), "file exists");
    long sz = mmc_file_size(tmpfile);
    TEST_ASSERT(sz > 0, "file size > 0");

    f = mmc_file_open(tmpfile, "r");
    TEST_ASSERT(f != NULL, "file open read");
    char *content = mmc_file_read_all(f);
    TEST_ASSERT(content != NULL && strstr(content, "Hello") != NULL, "file read");
    mmc_file_close(f);

    f = mmc_file_open(tmpfile, "r");
    MMC_Array *lines = mmc_file_read_lines(f);
    TEST_ASSERT(mmc_arr_len(lines) == 2, "read 2 lines");
    mmc_arr_free(lines);
    mmc_file_close(f);

    remove(tmpfile);
}

static void test_section10_platform(void)
{
    printf("  Section 10: Platform...\n");

    const char *plat = mmc_platform_name();
    TEST_ASSERT(plat != NULL && plat[0] != '\0', "platform name");

    const char *arch = mmc_arch_name();
    TEST_ASSERT(arch != NULL && arch[0] != '\0', "arch name");

    long long ms = mmc_time_ms();
    TEST_ASSERT(ms > 0, "time_ms > 0");

    long long sec = mmc_time_sec();
    TEST_ASSERT(sec > 1700000000LL, "time_sec reasonable");

    /* Sleep briefly */
    long long before = mmc_time_ms();
    mmc_sleep_ms(10);
    long long after = mmc_time_ms();
    TEST_ASSERT(after >= before, "sleep works");
}

int main(void)
{
    printf("=== MMC Runtime Library Self-Test ===\n\n");

    selftest_pass = 0;
    selftest_fail = 0;

    MMC_Error err = mmc_runtime_init();
    if (err != MMC_OK) {
        printf("FATAL: mmc_runtime_init failed with error %d\n", err);
        return 1;
    }

    test_section2_values();
    test_section3_array();
    test_section4_dict();
    test_section5_strings();
    test_section6_arena();
    test_section8_range();
    test_section9_file();
    test_section10_platform();

    mmc_runtime_cleanup();

    printf("\n=== Results: %d passed, %d failed ===\n",
           selftest_pass, selftest_fail);

    return selftest_fail > 0 ? 1 : 0;
}

#endif /* MMC_RUNTIME_SELFTEST */

/*
 * ============================================================================
 *  End of mmclib.c  v2.0.0
 *  MMC Compiler Project  -  Nyanlin-AI Self-Hosting Runtime
 * ============================================================================
 */
