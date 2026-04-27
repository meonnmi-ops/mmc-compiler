/*
 * ============================================================================
 *  mmclib.h  -  MMC C Runtime Library
 * ============================================================================
 *
 *  Project:    MMC Compiler  -  Self-Hosted Runtime
 *  Context:    အေအိုင်AI / Nyanlin-AI
 *
 *  Description:
 *      Full C runtime library for MMC programs compiled to C.
 *      Provides Python-like dynamic types, strings, arrays, dicts,
 *      memory management, file I/O, and Myanmar Unicode support.
 *
 *  Architecture:
 *      MMC Source (.mmc) -> mmc_c_codegen.mmc -> C99 (+mmclib.h) -> Binary
 *
 *  Key Design Decisions:
 *      - MMC_Value: tagged union for dynamic typing (int/double/str/bool/none/array/dict)
 *      - Arena allocator: fast bulk allocation with simple reset
 *      - Myanmar Unicode: UTF-8 aware string operations
 *      - No external dependencies beyond C99 standard library
 *
 *  Compatibility:
 *      - C99 (GCC / Clang / MSVC)
 *      - Linux, macOS, Windows, Android (NDK), iOS
 *      - ARM-v8, x86_64, RISC-V
 *
 *  Version:    2.0.0
 *  License:    MIT
 *  Author:     MMC Compiler Team / Nyanlin-AI
 * ============================================================================
 */

#ifndef MMC_RUNTIME_LIB_H
#define MMC_RUNTIME_LIB_H

#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 *  Section 1: Constants and Limits
 * ============================================================================ */

#define MMC_VERSION_MAJOR    2
#define MMC_VERSION_MINOR    0
#define MMC_VERSION_PATCH    0

#define MMC_STR_BUF_SIZE     65536    /* Default string buffer size     */
#define MMC_ARR_INIT_SIZE    16       /* Initial array capacity         */
#define MMC_DICT_INIT_SIZE   32       /* Initial dict bucket count      */
#define MMC_ARENA_BLOCK_SIZE (256 * 1024)  /* Arena block size: 256 KB   */
#define MMC_MAX_NEST_DEPTH   64       /* Max recursion depth            */

/* ============================================================================
 *  Section 2: MMC Dynamic Value Type
 * ============================================================================ *
 *  MMC_Value is a tagged union that can hold any MMC value type.
 *  This enables Python-like dynamic behavior in compiled C code.
 *
 *  Type tags:
 *      MMC_INT    - signed 64-bit integer
 *      MMC_FLOAT  - double-precision floating point
 *      MMC_STRING - null-terminated UTF-8 string (Myanmar-aware)
 *      MMC_BOOL   - 0 or 1
 *      MMC_NONE   - null/void equivalent (ဗလာ / ဘာမှမရှိ)
 *      MMC_ARRAY  - dynamic array of MMC_Value
 *      MMC_DICT   - hash map of string -> MMC_Value
 * ============================================================================ */

typedef enum {
    MMC_TYPE_INT    = 0,
    MMC_TYPE_FLOAT  = 1,
    MMC_TYPE_STRING = 2,
    MMC_TYPE_BOOL   = 3,
    MMC_TYPE_NONE   = 4,
    MMC_TYPE_ARRAY  = 5,
    MMC_TYPE_DICT   = 6,
} MMC_Type;

/* Forward declarations */
struct MMC_Array;
struct MMC_Dict;

typedef struct MMC_Value {
    MMC_Type type;
    union {
        long long      int_val;    /* MMC_TYPE_INT    */
        double         float_val;  /* MMC_TYPE_FLOAT  */
        const char    *str_val;    /* MMC_TYPE_STRING */
        int            bool_val;   /* MMC_TYPE_BOOL   */
        struct MMC_Array  *arr_val;   /* MMC_TYPE_ARRAY */
        struct MMC_Dict   *dict_val;  /* MMC_TYPE_DICT */
    };
} MMC_Value;


/* --- MMC Value Constructors --- */

/* Create values of each type */
MMC_Value mmc_int(long long v);
MMC_Value mmc_float(double v);
MMC_Value mmc_string(const char *s);
MMC_Value mmc_bool(int v);
MMC_Value mmc_none(void);
MMC_Value mmc_array(void);
MMC_Value mmc_dict(void);

/* --- MMC Value Accessors --- */

/* Safe type-checked access (returns 0/NULL on type mismatch) */
long long      mmc_as_int(MMC_Value v);
double         mmc_as_float(MMC_Value v);
const char    *mmc_as_string(MMC_Value v);
int            mmc_as_bool(MMC_Value v);

/* --- MMC Value Operations --- */

/* Type checking */
int mmc_is_int(MMC_Value v);
int mmc_is_float(MMC_Value v);
int mmc_is_string(MMC_Value v);
int mmc_is_bool(MMC_Value v);
int mmc_is_none(MMC_Value v);
int mmc_is_array(MMC_Value v);
int mmc_is_dict(MMC_Value v);

/* Type name as string */
const char *mmc_type_name(MMC_Value v);

/* Value equality comparison */
int mmc_values_equal(MMC_Value a, MMC_Value b);

/* Convert value to string representation */
const char *mmc_value_to_str(MMC_Value v);

/* Print a single value with automatic format detection */
void mmc_print_value(MMC_Value v);

/* Print a formatted value with newline */
void mmc_println_value(MMC_Value v);


/* ============================================================================
 *  Section 3: MMC Dynamic Array
 * ============================================================================ *
 *  Growable array of MMC_Value elements.
 *  Supports push, pop, get, set, length, and iteration.
 * ============================================================================ */

typedef struct MMC_Array {
    MMC_Value *data;      /* Heap-allocated element storage    */
    size_t     len;       /* Current number of elements        */
    size_t     capacity;  /* Allocated capacity                */
} MMC_Array;

/* Create/destroy */
MMC_Array *mmc_arr_new(void);
MMC_Array *mmc_arr_new_capacity(size_t cap);
void       mmc_arr_free(MMC_Array *arr);
void       mmc_arr_clear(MMC_Array *arr);

/* Element access */
MMC_Value mmc_arr_get(MMC_Array *arr, size_t index);
int       mmc_arr_set(MMC_Array *arr, size_t index, MMC_Value val);
MMC_Value mmc_arr_front(MMC_Array *arr);
MMC_Value mmc_arr_back(MMC_Array *arr);

/* Modification */
int mmc_arr_push(MMC_Array *arr, MMC_Value val);
MMC_Value mmc_arr_pop(MMC_Array *arr);
int mmc_arr_insert(MMC_Array *arr, size_t index, MMC_Value val);
int mmc_arr_remove(MMC_Array *arr, size_t index);
int mmc_arr_append_all(MMC_Array *arr, MMC_Array *other);

/* Searching */
long mmc_arr_index_of(MMC_Array *arr, MMC_Value val);
int   mmc_arr_contains(MMC_Array *arr, MMC_Value val);
int   mmc_arr_count(MMC_Array *arr, MMC_Value val);

/* Info */
size_t mmc_arr_len(MMC_Array *arr);
int    mmc_arr_is_empty(MMC_Array *arr);
void   mmc_arr_reverse(MMC_Array *arr);

/* Sorting */
void mmc_arr_sort_int(MMC_Array *arr);
void mmc_arr_sort_float(MMC_Array *arr);

/* Slicing (returns new array) */
MMC_Array *mmc_arr_slice(MMC_Array *arr, long start, long end);


/* ============================================================================
 *  Section 4: MMC Dictionary (Hash Map)
 * ============================================================================ *
 *  String-keyed hash map storing MMC_Value entries.
 *  Uses open addressing with linear probing for collision resolution.
 * ============================================================================ */

typedef struct MMC_Dict_Entry {
    const char *key;       /* Heap-allocated key string   */
    MMC_Value   value;     /* Associated value            */
    int         occupied;  /* 1 if slot is in use         */
} MMC_Dict_Entry;

typedef struct MMC_Dict {
    MMC_Dict_Entry *buckets;   /* Hash table storage       */
    size_t          count;     /* Number of entries        */
    size_t          capacity;  /* Number of buckets        */
} MMC_Dict;

/* Create/destroy */
MMC_Dict *mmc_dict_new(void);
MMC_Dict *mmc_dict_new_capacity(size_t cap);
void      mmc_dict_free(MMC_Dict *dict);
void      mmc_dict_clear(MMC_Dict *dict);

/* Access */
MMC_Value mmc_dict_get(MMC_Dict *dict, const char *key);
MMC_Value mmc_dict_get_default(MMC_Dict *dict, const char *key, MMC_Value default_val);
int       mmc_dict_set(MMC_Dict *dict, const char *key, MMC_Value val);
int       mmc_dict_delete(MMC_Dict *dict, const char *key);

/* Query */
int    mmc_dict_has_key(MMC_Dict *dict, const char *key);
size_t mmc_dict_len(MMC_Dict *dict);
int    mmc_dict_is_empty(MMC_Dict *dict);

/* Keys/Values iteration (returns arrays that must be freed by caller) */
MMC_Array *mmc_dict_keys(MMC_Dict *dict);
MMC_Array *mmc_dict_values(MMC_Dict *dict);


/* ============================================================================
 *  Section 5: String Operations (Myanmar Unicode Aware)
 * ============================================================================ *
 *  All string functions handle UTF-8 encoded text, including Myanmar
 *  characters (U+1000 - U+109F).  String memory is managed via
 *  the arena allocator for fast allocation.
 * ============================================================================ */

/* Basic operations */
size_t      mmc_str_len(const char *s);          /* Byte length             */
size_t      mmc_str_char_len(const char *s);     /* Unicode char count      */
char       *mmc_str_dup(const char *s);          /* Deep copy (arena)       */
char       *mmc_str_concat(const char *a, const char *b);  /* Concatenate  */
char       *mmc_str_sub(const char *s, long start, long end); /* Substring   */

/* Case conversion (ASCII only, Myanmar chars unchanged) */
char *mmc_str_upper(const char *s);
char *mmc_str_lower(const char *s);
char *mmc_str_title(const char *s);

/* Trimming */
char *mmc_str_strip(const char *s);
char *mmc_str_lstrip(const char *s);
char *mmc_str_rstrip(const char *s);

/* Searching */
long  mmc_str_find(const char *haystack, const char *needle);
long  mmc_str_rfind(const char *haystack, const char *needle);
int   mmc_str_starts_with(const char *s, const char *prefix);
int   mmc_str_ends_with(const char *s, const char *suffix);
int   mmc_str_contains(const char *haystack, const char *needle);
int   mmc_str_count(const char *haystack, const char *needle);

/* Modification */
char *mmc_str_replace(const char *s, const char *old, const char *new_s);
char *mmc_str_replace_all(const char *s, const char *old, const char *new_s);

/* Split/Join */
MMC_Array *mmc_str_split(const char *s, const char *delim);
MMC_Array *mmc_str_split_lines(const char *s);
char      *mmc_str_join(MMC_Array *arr, const char *delim);

/* Character classification (Unicode-aware) */
int mmc_str_isalpha(const char *s);
int mmc_str_isdigit(const char *s);
int mmc_str_isalnum(const char *s);
int mmc_str_isspace(const char *s);
int mmc_str_isupper(const char *s);
int mmc_str_islower(const char *s);

/* Myanmar-specific */
int mmc_str_is_myanmar(const char *s);
int mmc_char_is_myanmar(int codepoint);
char *mmc_str_myanmar_digits_to_latin(const char *s);
char *mmc_str_latin_digits_to_myanmar(const char *s);


/* ============================================================================
 *  Section 6: Memory Management (Arena Allocator)
 * ============================================================================ *
 *  Arena allocator for fast bulk memory allocation.
 *  All memory in a block is freed at once when the arena is reset.
 *  Ideal for compilation and short-lived data structures.
 * ============================================================================ */

typedef struct MMC_Arena_Block {
    char                  *data;
    size_t                 used;
    size_t                 size;
    struct MMC_Arena_Block *next;
} MMC_Arena_Block;

typedef struct MMC_Arena {
    MMC_Arena_Block *current;
    size_t           total_allocated;
} MMC_Arena;

MMC_Arena *mmc_arena_new(void);
void      *mmc_arena_alloc(MMC_Arena *arena, size_t size);
char      *mmc_arena_strdup(MMC_Arena *arena, const char *s);
void       mmc_arena_reset(MMC_Arena *arena);
void       mmc_arena_free(MMC_Arena *arena);

/* Global default arena (for convenience) */
MMC_Arena *mmc_get_global_arena(void);
void       mmc_global_arena_reset(void);


/* ============================================================================
 *  Section 7: Print and Format Functions
 * ============================================================================ *
 *  Python-style print() and format() implementations for MMC.
 * ============================================================================ */

/* Print multiple values separated by spaces, ending with newline */
void mmc_print(int argc, MMC_Value *argv);
void mmc_print_sep(const char *sep, int argc, MMC_Value *argv);

/* Print without newline */
void mmc_print_no_nl(int argc, MMC_Value *argv);

/* Formatted print (printf-like but with MMC_Value args) */
void mmc_printf(const char *fmt, ...);

/* Input (reads a line from stdin) */
MMC_Value mmc_input_value(const char *prompt);
const char *mmc_input(const char *prompt);


/* ============================================================================
 *  Section 8: Range Iterator
 * ============================================================================ *
 *  Python-style range() for for loops.
 *  Supports range(stop), range(start, stop), range(start, stop, step).
 * ============================================================================ */

typedef struct MMC_Range {
    long long current;
    long long stop;
    long long step;
    int       done;
} MMC_Range;

MMC_Range mmc_range(long long stop);
MMC_Range mmc_range_start(long long start, long long stop);
MMC_Range mmc_range_step(long long start, long long stop, long long step);
long long mmc_range_next(MMC_Range *r);
int       mmc_range_done(MMC_Range *r);


/* ============================================================================
 *  Section 9: File I/O
 * ============================================================================ *
 *  Cross-platform file operations for MMC programs.
 * ============================================================================ */

typedef struct MMC_File {
    FILE *fp;
    char  path[4096];
    char  mode[16];
} MMC_File;

MMC_File *mmc_file_open(const char *path, const char *mode);
int       mmc_file_close(MMC_File *f);
char     *mmc_file_read_all(MMC_File *f);
MMC_Array *mmc_file_read_lines(MMC_File *f);
int       mmc_file_write(MMC_File *f, const char *data);
int       mmc_file_write_line(MMC_File *f, const char *line);
long      mmc_file_tell(MMC_File *f);
int       mmc_file_seek(MMC_File *f, long offset, int whence);
int       mmc_file_exists(const char *path);
long      mmc_file_size(const char *path);


/* ============================================================================
 *  Section 10: System and Platform
 * ============================================================================ */

/* Platform detection */
const char *mmc_platform_name(void);
const char *mmc_arch_name(void);

/* Time */
long long mmc_time_ms(void);         /* Milliseconds since epoch */
long long mmc_time_sec(void);        /* Seconds since epoch      */
void      mmc_sleep_ms(long long ms);
void      mmc_sleep_sec(double sec);

/* Environment */
const char *mmc_getenv(const char *name);
int         mmc_setenv(const char *name, const char *value);


/* ============================================================================
 *  Section 11: Error Handling
 * ============================================================================ */

typedef enum {
    MMC_OK           =  0,
    MMC_ERR_MEMORY   = -1,
    MMC_ERR_INDEX    = -2,
    MMC_ERR_TYPE     = -3,
    MMC_ERR_KEY      = -4,
    MMC_ERR_IO       = -5,
    MMC_ERR_OVERFLOW = -6,
    MMC_ERR_DIV_ZERO = -7,
    MMC_ERR_RUNTIME  = -8,
} MMC_Error;

const char *mmc_error_name(MMC_Error err);
const char *mmc_error_msg(MMC_Error err);

/* Last error tracking */
void       mmc_set_last_error(MMC_Error err);
MMC_Error  mmc_get_last_error(void);


/* ============================================================================
 *  Section 12: MMC Runtime Initialization
 * ============================================================================ */

/**
 * mmc_runtime_init  -  Initialize the MMC C Runtime
 *
 * Sets up the global arena, error handler, and platform detection.
 * Must be called before any MMC runtime function is used.
 * Typically called as the first line of main() in compiled MMC programs.
 *
 * Returns: MMC_OK on success.
 */
MMC_Error mmc_runtime_init(void);

/**
 * mmc_runtime_cleanup  -  Clean up the MMC C Runtime
 *
 * Frees all arena memory and resets the global state.
 * Should be called before program exit.
 *
 * Returns: MMC_OK on success.
 */
MMC_Error mmc_runtime_cleanup(void);


/* ============================================================================
 *  Section 13: Convenience Macros for MMC Code Generator
 * ============================================================================ *
 *  These macros are used by mmc_c_codegen.mmc to emit common patterns.
 * ============================================================================ */

/* Print a single string with newline */
#define MMC_PRINT_STR(s)       printf("%s\n", (s))
#define MMC_PRINT_INT(n)       printf("%lld\n", (long long)(n))
#define MMC_PRINT_FLOAT(f)     printf("%f\n", (double)(f))
#define MMC_PRINT_BOOL(b)      printf("%s\n", (b) ? "true" : "false")
#define MMC_PRINT_NONE()       printf("None\n")

/* Print without newline */
#define MMC_PUT_STR(s)         printf("%s", (s))
#define MMC_PUT_INT(n)         printf("%lld", (long long)(n))

/* Quick value construction in generated code */
#define MMC_VAL_INT(n)         mmc_int((long long)(n))
#define MMC_VAL_FLOAT(f)       mmc_float((double)(f))
#define MMC_VAL_STR(s)         mmc_string((const char *)(s))
#define MMC_VAL_BOOL(b)        mmc_bool((int)(b))
#define MMC_VAL_NONE()         mmc_none()


#ifdef __cplusplus
}
#endif

#endif /* MMC_RUNTIME_LIB_H */

/*
 * ============================================================================
 *  End of mmclib.h  v2.0.0
 *  MMC Compiler Project  -  Nyanlin-AI Self-Hosting Runtime
 * ============================================================================
 */
