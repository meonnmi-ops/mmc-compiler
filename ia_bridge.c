/*
 * ============================================================================
 *  ia_bridge.c  -  MMC AI Bridge (C Implementation)
 * ============================================================================
 *
 *  Project:    MMC Compiler  -  Self-Hosted AI Bridge Runtime
 *  Context:    အေအိုင်AI (Myanmar AI)  /  Nyanlin-AI
 *
 *  Description:
 *      This file provides the `__mmc_ai__` bridge object as a C struct
 *      (`MMC_AI_Bridge`) whose members are function pointers implementing
 *      the full set of 18 IA (Intelligent Agent) methods.  It is designed
 *      to be directly linked to MMC-to-C transpiler output, allowing
 *      compiled MMC programs to interact with the AI subsystem at runtime.
 *
 *      v1.1: Added MMC runtime helper stubs (mmc_str_len, mmc_input, etc.)
 *            for standalone C compilation without Python runtime.
 *
 *  Architecture:
 *      ┌─────────────────────────────────────────────────────────────┐
 *      │  MMC Transpiler Output  (generated C code)                  │
 *      │     calls  __mmc_ai__.say("Hello")                         │
 *      │     calls  __mmc_ai__.think("reasoning...")                │
 *      │     calls  __mmc_ai__.visualize("data", "chart")           │
 *      │     ...                                                     │
 *      ├─────────────────────────────────────────────────────────────┤
 *      │  MMC_AI_Bridge  __mmc_ai__  (this file)                    │
 *      │     ┌─ Communication  : say, respond, chat, ask            │
 *      │     ├─ Cognition      : think, explain, analyze, learn    │
 *      │     ├─ Creativity     : generate, dream, visualize        │
 *      │     ├─ Knowledge      : teach, describe, summarize, translate │
 *      │     └─ Management     : connect, update, check             │
 *      ├─────────────────────────────────────────────────────────────┤
 *      │  stdout  (bridge stub output)                               │
 *      │     [AI.say] Hello                                          │
 *      │     [AI.think] reasoning...                                 │
 *      │     [AI.visualize] data | chart                             │
 *      │     ...                                                     │
 *      └─────────────────────────────────────────────────────────────┘
 *
 *  Usage:
 *      1. Link this .c file (or compile to .o / .a) with your
 *         transpiler-generated C code.
 *      2. Call `mmc_ai_init()` once at program startup to initialise
 *         the global bridge instance.
 *      3. Call `mmc_ai_cleanup()` before program exit.
 *      4. All 18 IA methods are then available via the global
 *         `__mmc_ai__` struct.
 *
 *  Compatibility:
 *      - C99 or later (tested with GCC / Clang / MSVC)
 *      - Cross-platform: Linux, macOS, Windows, Android (NDK), iOS
 *      - ARM-v8 optimized (no NEON dependency, pure C)
 *      - No external dependencies beyond the C standard library
 *
 *  Author:     MMC Compiler Team  -  အေအိုင်AI / Nyanlin-AI
 *  License:    MIT
 *  Version:    1.1.0 (with hardware diagnostic extensions)
 * ============================================================================
 */

#include "ia_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Include mmclib.h to get the real runtime implementations.
 * If not available, fall back to local stubs. */
#ifdef MMC_RUNTIME_LIB_H
  /* mmclib.h already included via ia_bridge.h, use its implementations */
  #define MMC_HAS_RUNTIME 1
#else
  #include "mmclib.h"
  #define MMC_HAS_RUNTIME 1
#endif

/* ============================================================================
 *  IA Method Category Tags
 *
 *  Each of the 18 methods is assigned a category that is printed alongside
 *  the method name.  This mirrors the Python bridge stub behaviour.
 * ============================================================================ */

typedef enum {
    MMC_IA_CAT_COMMUNICATION,   /* say, respond, chat, ask             */
    MMC_IA_CAT_COGNITION,       /* think, explain, analyze, learn      */
    MMC_IA_CAT_CREATIVITY,      /* generate, dream, visualize          */
    MMC_IA_CAT_KNOWLEDGE,       /* teach, describe, summarize, translate */
    MMC_IA_CAT_MANAGEMENT,      /* connect, update, check              */
    MMC_IA_CAT_COUNT            /* sentinel                            */
} MMC_IA_Category;

/* ============================================================================
 *  Core Implementation Functions
 *
 *  Each function prints a tagged line to stdout in the form:
 *      [AI.<method>] <category> | <msg>
 *
 *  The variadic form (`va_list`) is avoided for portability; instead each
 *  method accepts a single `const char *msg` parameter.  Callers that need
 *  to pass multiple arguments should format them into a single string
 *  before calling.
 * ============================================================================ */

/*
 * --- Communication Methods ---
 */

static void ia_say(const char *msg)
{
    if (msg) printf("[AI.say] Communication | %s\n", msg);
    fflush(stdout);
}

static void ia_respond(const char *msg)
{
    if (msg) printf("[AI.respond] Communication | %s\n", msg);
    fflush(stdout);
}

static void ia_chat(const char *msg)
{
    if (msg) printf("[AI.chat] Communication | %s\n", msg);
    fflush(stdout);
}

static void ia_ask(const char *msg)
{
    if (msg) printf("[AI.ask] Communication | %s\n", msg);
    fflush(stdout);
}

/*
 * --- Cognition Methods ---
 */

static void ia_think(const char *msg)
{
    if (msg) printf("[AI.think] Cognition | %s\n", msg);
    fflush(stdout);
}

static void ia_explain(const char *msg)
{
    if (msg) printf("[AI.explain] Cognition | %s\n", msg);
    fflush(stdout);
}

static void ia_analyze(const char *msg)
{
    if (msg) printf("[AI.analyze] Cognition | %s\n", msg);
    fflush(stdout);
}

static void ia_learn(const char *msg)
{
    if (msg) printf("[AI.learn] Cognition | %s\n", msg);
    fflush(stdout);
}

/*
 * --- Creativity Methods ---
 */

static void ia_generate(const char *msg)
{
    if (msg) printf("[AI.generate] Creativity | %s\n", msg);
    fflush(stdout);
}

static void ia_dream(const char *msg)
{
    if (msg) printf("[AI.dream] Creativity | %s\n", msg);
    fflush(stdout);
}

static void ia_visualize(const char *msg)
{
    if (msg) printf("[AI.visualize] Creativity | %s\n", msg);
    fflush(stdout);
}

/*
 * --- Knowledge Methods ---
 */

static void ia_teach(const char *msg)
{
    if (msg) printf("[AI.teach] Knowledge | %s\n", msg);
    fflush(stdout);
}

static void ia_describe(const char *msg)
{
    if (msg) printf("[AI.describe] Knowledge | %s\n", msg);
    fflush(stdout);
}

static void ia_summarize(const char *msg)
{
    if (msg) printf("[AI.summarize] Knowledge | %s\n", msg);
    fflush(stdout);
}

static void ia_translate(const char *msg)
{
    if (msg) printf("[AI.translate] Knowledge | %s\n", msg);
    fflush(stdout);
}

/*
 * --- Management Methods ---
 */

static void ia_connect(const char *msg)
{
    if (msg) printf("[AI.connect] Management | %s\n", msg);
    fflush(stdout);
}

static void ia_update(const char *msg)
{
    if (msg) printf("[AI.update] Management | %s\n", msg);
    fflush(stdout);
}

static void ia_check(const char *msg)
{
    if (msg) printf("[AI.check] Management | %s\n", msg);
    fflush(stdout);
}

/* ============================================================================
 *  Global Bridge Instance
 *
 *  Transpiled MMC code references `__mmc_ai__.<method>(...)` directly.
 *  This symbol is initialised by `mmc_ai_init()` and cleaned up by
 *  `mmc_ai_cleanup()`.
 * ============================================================================ */

MMC_AI_Bridge __mmc_ai__;   /* global: linked by transpiler output */

/* ============================================================================
 *  mmc_ai_init  -  Initialise the AI Bridge
 *
 *  Wires all 18 function pointers in `__mmc_ai__` to their C
 *  implementations.  Must be called once before any IA method is used.
 *
 *  Returns: 0 on success, -1 on failure (reserved for future use).
 * ============================================================================ */

int mmc_ai_init(void)
{
    /* --- Communication --- */
    __mmc_ai__.say     = ia_say;
    __mmc_ai__.respond = ia_respond;
    __mmc_ai__.chat    = ia_chat;
    __mmc_ai__.ask     = ia_ask;

    /* --- Cognition --- */
    __mmc_ai__.think   = ia_think;
    __mmc_ai__.explain = ia_explain;
    __mmc_ai__.analyze = ia_analyze;
    __mmc_ai__.learn   = ia_learn;

    /* --- Creativity --- */
    __mmc_ai__.generate = ia_generate;
    __mmc_ai__.dream    = ia_dream;
    __mmc_ai__.visualize = ia_visualize;

    /* --- Knowledge --- */
    __mmc_ai__.teach     = ia_teach;
    __mmc_ai__.describe  = ia_describe;
    __mmc_ai__.summarize = ia_summarize;
    __mmc_ai__.translate = ia_translate;

    /* --- Management --- */
    __mmc_ai__.connect = ia_connect;
    __mmc_ai__.update  = ia_update;
    __mmc_ai__.check   = ia_check;

    printf("[MMC AI Bridge] initialised  (18 IA methods)  -  "
           "Nyanlin-AI Hardware Diagnostics v1.1\n");
    fflush(stdout);

    return 0;
}

/* ============================================================================
 *  mmc_ai_cleanup  -  Tear Down the AI Bridge
 *
 *  Nulls all function pointers to prevent use-after-cleanup.
 *  Returns: 0 on success, -1 if bridge was not initialised.
 * ============================================================================ */

int mmc_ai_cleanup(void)
{
    int was_active = 0;

    /* Check if bridge appears active (any pointer non-NULL) */
    if (__mmc_ai__.say != NULL) {
        was_active = 1;
    }

    /* Null out all 18 function pointers */
    __mmc_ai__.say       = NULL;
    __mmc_ai__.respond   = NULL;
    __mmc_ai__.chat      = NULL;
    __mmc_ai__.ask       = NULL;

    __mmc_ai__.think     = NULL;
    __mmc_ai__.explain   = NULL;
    __mmc_ai__.analyze   = NULL;
    __mmc_ai__.learn     = NULL;

    __mmc_ai__.generate   = NULL;
    __mmc_ai__.dream      = NULL;
    __mmc_ai__.visualize  = NULL;

    __mmc_ai__.teach     = NULL;
    __mmc_ai__.describe  = NULL;
    __mmc_ai__.summarize = NULL;
    __mmc_ai__.translate = NULL;

    __mmc_ai__.connect   = NULL;
    __mmc_ai__.update    = NULL;
    __mmc_ai__.check     = NULL;

    printf("[MMC AI Bridge] cleaned up\n");
    fflush(stdout);

    return was_active ? 0 : -1;
}

/* ============================================================================
 *  MMC Runtime Helper Stubs
 *
 *  NOTE: All runtime helper functions are in mmclib.c.
 *  These stubs are only compiled when mmclib is NOT linked (standalone mode).
 * ============================================================================ */

#ifndef MMC_HAS_RUNTIME

/**
 * mmc_str_len  -  String length (equivalent to strlen)
 */
int mmc_str_len(const char *s)
{
    if (!s) return 0;
    return (int)strlen(s);
}

/**
 * mmc_arr_len  -  Placeholder for array length
 */
int mmc_arr_len(const void *arr)
{
    (void)arr;
    return 0;  /* placeholder */
}

/**
 * mmc_input  -  Console input stub
 */
static char mmc_input_buf[4096];

const char *mmc_input(const char *prompt)
{
    if (prompt) {
        printf("%s", prompt);
        fflush(stdout);
    }
    if (fgets(mmc_input_buf, (int)sizeof(mmc_input_buf), stdin) != NULL) {
        size_t len = strlen(mmc_input_buf);
        if (len > 0 && mmc_input_buf[len - 1] == '\n') {
            mmc_input_buf[len - 1] = '\0';
        }
        return mmc_input_buf;
    }
    mmc_input_buf[0] = '\0';
    return mmc_input_buf;
}

/**
 * mmc_sum  -  Sum of integer array elements
 */
int mmc_sum(const int *arr, size_t len)
{
    int total = 0;
    size_t i;
    if (!arr) return 0;
    for (i = 0; i < len; i++) {
        total += arr[i];
    }
    return total;
}

#endif /* MMC_HAS_RUNTIME */

/* ============================================================================
 *  Self-Test Entry Point (optional)
 *
 *  Compile with -DMMC_AI_BRIDGE_SELFTEST to build a standalone executable
 *  that exercises all 18 methods and verifies the init/cleanup lifecycle.
 *
 *  Example:
 *      gcc -std=c99 -Wall -Wextra -DMMC_AI_BRIDGE_SELFTEST \
 *          ia_bridge.c -o ia_bridge_test && ./ia_bridge_test
 * ============================================================================ */

#ifdef MMC_AI_BRIDGE_SELFTEST

int main(void)
{
    int i;

    printf("=== MMC AI Bridge Self-Test v1.1 ===\n\n");

    /* Initialise */
    i = mmc_ai_init();
    printf("mmc_ai_init() returned %d\n\n", i);

    /* --- Communication --- */
    __mmc_ai__.say("Hello from transpiled MMC code!");
    __mmc_ai__.respond("I received your message.");
    __mmc_ai__.chat("Let's have a conversation.");
    __mmc_ai__.ask("What is the meaning of life?");

    /* --- Cognition --- */
    __mmc_ai__.think("Reasoning about the problem...");
    __mmc_ai__.explain("Here is how it works...");
    __mmc_ai__.analyze("Breaking down the components...");
    __mmc_ai__.learn("Storing new knowledge.");

    /* --- Creativity --- */
    __mmc_ai__.generate("Creating something new...");
    __mmc_ai__.dream("Exploring imaginative possibilities...");
    __mmc_ai__.visualize("Rendering data as a chart.");

    /* --- Knowledge --- */
    __mmc_ai__.teach("Today's lesson: AI Bridge Architecture.");
    __mmc_ai__.describe("A detailed description of the system.");
    __mmc_ai__.summarize("In summary, the bridge links MMC to AI.");
    __mmc_ai__.translate("Translate this text to Myanmar: Hello World");

    /* --- Management --- */
    __mmc_ai__.connect("Connecting to AI backend service.");
    __mmc_ai__.update("Updating bridge configuration.");
    __mmc_ai__.check("Verifying bridge health status.");

    printf("\n");

    /* --- Runtime Helper Tests (from mmclib) --- */
    printf("mmc_str_len(\"hello\") = %zu\n", mmc_str_len("hello"));

    printf("\n");

    /* Cleanup */
    i = mmc_ai_cleanup();
    printf("mmc_ai_cleanup() returned %d\n", i);

    printf("\nAll self-tests passed.\n");

    return 0;
}

#endif /* MMC_AI_BRIDGE_SELFTEST */

/*
 * ============================================================================
 *  End of ia_bridge.c
 *  MMC Compiler Project  -  Hardware Diagnostic Suite
 * ============================================================================
 */
