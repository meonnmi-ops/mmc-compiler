/*
 * ============================================================================
 *  ia_bridge.h  -  MMC AI Bridge Header
 * ============================================================================
 *
 *  Project:    MMC Compiler  -  Self-Hosted AI Bridge Runtime
 *  Context:    အေအိုင်AI (Myanmar AI) / Nyanlin-AI
 *
 *  Description:
 *      Public header for the `__mmc_ai__` bridge object.  This file is
 *      included by transpiled MMC-to-C output and by ia_bridge.c.
 *
 *  Architecture:
 *      Transpiled MMC C code  --#include "ia_bridge.h"-->  ia_bridge.c
 *
 *      __mmc_ai__.say("msg")    =>    bridge.say("msg")    => stdout
 *      __mmc_ai__.think("msg")  =>    bridge.think("msg")  => stdout
 *      __mmc_ai__.analyze("msg")=>    bridge.analyze("msg")=> stdout
 *
 *  Compatibility:
 *      - C99 or later (GCC / Clang / MSVC)
 *      - Cross-platform: Linux, macOS, Windows, Android (NDK), iOS
 *      - ARM-v8 ready (no NEON dependency, pure C)
 *
 *  Author:     MMC Compiler Team
 *  License:    MIT
 *  Version:    1.1.0 (with hardware diagnostic extensions)
 * ============================================================================
 */

#ifndef MMC_AI_BRIDGE_H
#define MMC_AI_BRIDGE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 *  MMC_IA_Bridge Struct Definition
 *
 *  All 18 IA methods as function pointers.
 *  Uniform signature: void (*)(const char *msg)
 *
 *  Categories:
 *      Communication : say, respond, chat, ask
 *      Cognition     : think, explain, analyze, learn
 *      Creativity    : generate, dream, visualize
 *      Knowledge     : teach, describe, summarize, translate
 *      Management    : connect, update, check
 * ============================================================================ */

typedef struct MMC_AI_Bridge {

    /* --- Communication (4 methods) --- */
    void (*say)(const char *msg);
    void (*respond)(const char *msg);
    void (*chat)(const char *msg);
    void (*ask)(const char *msg);

    /* --- Cognition (4 methods) --- */
    void (*think)(const char *msg);
    void (*explain)(const char *msg);
    void (*analyze)(const char *msg);
    void (*learn)(const char *msg);

    /* --- Creativity (3 methods) --- */
    void (*generate)(const char *msg);
    void (*dream)(const char *msg);
    void (*visualize)(const char *msg);

    /* --- Knowledge (4 methods) --- */
    void (*teach)(const char *msg);
    void (*describe)(const char *msg);
    void (*summarize)(const char *msg);
    void (*translate)(const char *msg);

    /* --- Management (3 methods) --- */
    void (*connect)(const char *msg);
    void (*update)(const char *msg);
    void (*check)(const char *msg);

} MMC_AI_Bridge;


/* ============================================================================
 *  Global Bridge Instance
 *
 *  Declared in ia_bridge.c, referenced by transpiled MMC output.
 *  Must call mmc_ai_init() before any IA method usage.
 * ============================================================================ */

extern MMC_AI_Bridge __mmc_ai__;


/* ============================================================================
 *  Lifecycle Functions
 * ============================================================================ */

/**
 * mmc_ai_init  -  Initialise the AI Bridge
 *
 * Wires all 18 function pointers to their C implementations.
 * Must be called once at program startup (typically first line of main()).
 *
 * Returns: 0 on success, -1 on failure (reserved).
 */
int mmc_ai_init(void);

/**
 * mmc_ai_cleanup  -  Tear Down the AI Bridge
 *
 * Nulls all function pointers to prevent use-after-cleanup.
 *
 * Returns: 0 if bridge was active, -1 if not initialised.
 */
int mmc_ai_cleanup(void);


/* ============================================================================
 *  MMC Runtime Helper Stubs
 *
 *  NOTE: Core runtime functions (mmc_str_len, mmc_arr_len, mmc_input, etc.)
 *  are now provided by mmclib.h.  Only ia_bridge-specific stubs remain here.
 *  Include mmclib.h before ia_bridge.h to avoid conflicts.
 * ============================================================================ */

/* Stub declarations for backward compatibility - implementations in mmclib.c */
/* mmc_str_len, mmc_arr_len, mmc_input, mmc_sum are all in mmclib.c */


#ifdef __cplusplus
}
#endif

#endif /* MMC_AI_BRIDGE_H */

/*
 * ============================================================================
 *  End of ia_bridge.h
 *  အေအိုင်AI / Nyanlin-AI  -  MMC Compiler Project
 * ============================================================================
 */
