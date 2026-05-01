/*
 * ============================================================================
 *  mmc.h  -  MMC Compiler Unified Header
 * ============================================================================
 *
 *  Project:    MMC Compiler v8.1 - Myanmar Programming Language Transpiler
 *  Author:     MMC Compiler Team / Nyanlin-AI
 *  License:    MIT
 *  Version:    8.1.0
 *
 *  Description:
 *      Unified umbrella header for the MMC C Runtime.
 *      Includes both mmclib.h (core runtime) and ia_bridge.h (AI bridge).
 *
 *  Architecture:
 *      MMC Source (.mmc) → mmc_c_codegen.mmc → C99 (+mmc.h) → Binary
 *
 *  Usage:
 *      #include "mmc.h"   // includes everything
 *
 *  Compilation:
 *      gcc -std=c99 -lm -o program output.c mmclib.c ia_bridge.c
 *
 *  Compatibility:
 *      - C99 (GCC / Clang / MSVC)
 *      - Linux, macOS, Windows, Android (NDK), iOS
 *      - ARM-v8, x86_64, RISC-V
 *
 * ============================================================================
 */

#ifndef MMC_H
#define MMC_H

/* ============================================================================
 *  MMC Version
 * ============================================================================ */

#define MMC_VERSION       "8.1.0"
#define MMC_VERSION_MAJOR 8
#define MMC_VERSION_MINOR 1
#define MMC_VERSION_PATCH 0

/* Core runtime library */
#include "mmclib.h"

/* AI bridge (if available) */
#ifdef MMC_WITH_AI_BRIDGE
#include "ia_bridge.h"
#endif

#endif /* MMC_H */

/*
 * ============================================================================
 *  End of mmc.h  -  MMC Compiler Project / Nyanlin-AI
 * ============================================================================
 */
