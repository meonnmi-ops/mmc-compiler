#!/bin/bash
# =================================================================
# MMC Native Pipeline — Deployment Script (mmc_make.sh)
# Phase 4: MMC → Python / C compilation, self-hosting, and testing
#
# GitHub: https://github.com/meonnmi-ops/mmc-compiler
# =================================================================

set -euo pipefail

# ---------------------------------------------------------------------------
# Resolve directories relative to this script's location
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SELFHOSTED_DIR="${SCRIPT_DIR}/selfhosted"
COMPILE_MMC="${SCRIPT_DIR}/compile_mmc.py"
TRANSPILER="${SCRIPT_DIR}/mmc_transpiler.py"
TEST_FILE="${SCRIPT_DIR}/test_ia.mmc"

# ---------------------------------------------------------------------------
# Colour helpers (disabled when stdout is not a terminal)
# ---------------------------------------------------------------------------
if [[ -t 1 ]]; then
    RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
    CYAN='\033[0;36m'; BOLD='\033[1m'; RESET='\033[0m'
else
    RED='' GREEN='' YELLOW='' CYAN='' BOLD='' RESET=''
fi

info()  { echo -e "${CYAN}[mmc]${RESET} $*"; }
ok()    { echo -e "${GREEN}[ok]${RESET} $*"; }
warn()  { echo -e "${YELLOW}[warn]${RESET} $*" >&2; }
error() { echo -e "${RED}[error]${RESET} $*" >&2; }

# ---------------------------------------------------------------------------
# Pre-flight checks
# ---------------------------------------------------------------------------
check_python3() {
    if command -v python3 &>/dev/null; then
        PYTHON3="$(command -v python3)"
        PY_VERSION="$(python3 --version 2>&1)"
        return 0
    fi
    return 1
}

check_prerequisites() {
    if ! check_python3; then
        error "Python 3 is required but not found on PATH."
        error "Please install Python 3.8+ and try again."
        exit 127
    fi

    if [[ ! -f "${COMPILE_MMC}" ]]; then
        error "compile_mmc.py not found at ${COMPILE_MMC}"
        error "Make sure this script is in the mmc-compiler root directory."
        exit 1
    fi

    if [[ ! -d "${SELFHOSTED_DIR}" ]]; then
        error "selfhosted/ directory not found at ${SELFHOSTED_DIR}"
        exit 1
    fi
}

# ---------------------------------------------------------------------------
# Usage / help
# ---------------------------------------------------------------------------
show_usage() {
    cat <<EOF
${BOLD}MMC Native Pipeline v4.0${RESET} — အေအိုင်AI / Nyanlin-AI
GitHub: meonnmi-ops/mmc-compiler

${BOLD}USAGE${RESET}
    $(basename "$0") <command> [arguments]

${BOLD}COMMANDS${RESET}
    compile <file.mmc>       Compile MMC source to Python (native pipeline)
    run <file.mmc>           Compile MMC and execute the output
    ast <file.mmc>           Compile MMC and display the AST
    tokens <file.mmc>        Compile MMC and display the token stream
    c <file.mmc>             Compile MMC to C code (Phase 4)
    self-test                Run test_ia.mmc through the full pipeline
    self-compile             Self-compilation test (compile mmc_codegen.mmc)
    version                  Show version and environment info

${BOLD}EXAMPLES${RESET}
    $(basename "$0") compile examples/hello.mmc
    $(basename "$0") run     examples/hello.mmc
    $(basename "$0") ast     examples/if_else.mmc
    $(basename "$0") tokens  test_ia.mmc
    $(basename "$0") c       examples/hello.mmc
    $(basename "$0") self-test
EOF
}

# ---------------------------------------------------------------------------
# Validate that a file exists and is readable
# ---------------------------------------------------------------------------
require_file() {
    local filepath="$1"
    if [[ ! -f "${filepath}" ]]; then
        error "File not found: ${filepath}"
        exit 1
    fi
    if [[ ! -r "${filepath}" ]]; then
        error "File is not readable: ${filepath}"
        exit 1
    fi
}

# ---------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------

cmd_compile() {
    local target="$1"
    require_file "${target}"
    info "Compiling ${target} -> Python"
    "${PYTHON3}" "${COMPILE_MMC}" "${target}"
}

cmd_run() {
    local target="$1"
    require_file "${target}"
    info "Compiling and running ${target}"
    "${PYTHON3}" "${COMPILE_MMC}" "${target}" --run
}

cmd_ast() {
    local target="$1"
    require_file "${target}"
    info "Showing AST for ${target}"
    "${PYTHON3}" "${COMPILE_MMC}" "${target}" --ast
}

cmd_tokens() {
    local target="$1"
    require_file "${target}"
    info "Showing token stream for ${target}"
    "${PYTHON3}" "${COMPILE_MMC}" "${target}" --tokens
}

cmd_c() {
    local target="$1"
    require_file "${target}"

    # Phase 4 C backend — generate Python first, then transpile to C
    info "Phase 4: Compiling ${target} -> C"

    # Step 1: Generate Python via the native pipeline
    local py_code
    py_code="$("${PYTHON3}" "${COMPILE_MMC}" "${target}" 2>/dev/null)" || true

    if [[ -z "${py_code}" ]]; then
        # Fallback: use the transpiler directly
        warn "Native pipeline produced no output; falling back to mmc_transpiler.py"
        py_code="$("${PYTHON3}" "${TRANSPILER}" "${target}")"
    fi

    # Step 2: Basic C transpilation header
    local c_code
    c_code="/* ============================================================
 * MMC -> C Output (Phase 4)
 * Source: $(basename "${target}")
 * Generated by: MMC Native Pipeline v4.0
 * GitHub: meonnmi-ops/mmc-compiler
 * ============================================================ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- MMC C runtime stubs --- */
#define MMC_PRINT(fmt, ...) printf(fmt, ##__VA_ARGS__)
#define MMC_PRINTLN(fmt, ...) printf(fmt "\\n", ##__VA_ARGS__)

/* --- Compiled MMC code (as inline Python reference) --- */
/*
 * NOTE: Full C code-generation is in active development.
 *       The native pipeline currently produces Python; this
 *       stub provides the C scaffolding for Phase 4 completion.
 *
 * Equivalent Python output:
 * -------------------------
${py_code}
 * -------------------------
 */

int main(int argc, char *argv[]) {
    MMC_PRINTLN(\"MMC C backend (Phase 4) — source: %s\", \"$(basename "${target}")\");
    MMC_PRINTLN(\"Full C codegen is under active development.\");
    MMC_PRINTLN(\"Run './$(basename "$0") compile ${target}' for Python output.\");
    return 0;
}
"
    local out_c="${target%.mmc}.c"
    echo "${c_code}" > "${out_c}"
    ok "C output written to ${out_c}"
}

cmd_self_test() {
    require_file "${TEST_FILE}"
    info "Running self-test: ${TEST_FILE}"
    echo ""
    "${PYTHON3}" "${COMPILE_MMC}" "${TEST_FILE}" --run
    local rc=$?
    echo ""
    if [[ ${rc} -eq 0 ]]; then
        ok "self-test passed (exit code 0)"
    else
        error "self-test FAILED (exit code ${rc})"
    fi
    return ${rc}
}

cmd_self_compile() {
    local codegen_src="${SELFHOSTED_DIR}/mmc_codegen.mmc"
    require_file "${codegen_src}"

    info "Self-compilation test: compiling mmc_codegen.mmc through the pipeline"
    echo ""
    "${PYTHON3}" "${COMPILE_MMC}" "${codegen_src}"
    local rc=$?
    echo ""
    if [[ ${rc} -eq 0 ]]; then
        ok "self-compile succeeded — mmc_codegen.mmc compiles cleanly"
    else
        error "self-compile FAILED (exit code ${rc})"
    fi
    return ${rc}
}

cmd_version() {
    cat <<EOF
${BOLD}MMC Native Pipeline v4.0${RESET}
${CYAN}အေအိုင်AI${RESET} — ${CYAN}Nyanlin-AI${RESET}

  Compiler     : $(basename "${COMPILE_MMC}")
  Python       : ${PY_VERSION}
  Script dir   : ${SCRIPT_DIR}
  Selfhosted   : ${SELFHOSTED_DIR}
  Transpiler   : $(basename "${TRANSPILER}")$(if [[ -f "${TRANSPILER}" ]]; then echo " (present)"; else echo " (not found)"; fi)
  GitHub       : https://github.com/meonnmi-ops/mmc-compiler

  Pipeline stages:
    1. mmc_lexer.mmc      (MMC Source  -> Token[])
    2. mmc_parser.mmc     (Token[]     -> AST)
    3. mmc_codegen.mmc    (AST         -> Python)
    4. C backend          (Phase 4 — in development)
EOF
}

# ---------------------------------------------------------------------------
# Main dispatcher
# ---------------------------------------------------------------------------
main() {
    check_prerequisites

    local cmd="${1:-}"

    case "${cmd}" in
        compile)
            if [[ $# -lt 2 ]]; then
                error "'compile' requires a <file.mmc> argument"
                echo ""
                show_usage
                exit 1
            fi
            cmd_compile "$2"
            ;;
        run)
            if [[ $# -lt 2 ]]; then
                error "'run' requires a <file.mmc> argument"
                echo ""
                show_usage
                exit 1
            fi
            cmd_run "$2"
            ;;
        ast)
            if [[ $# -lt 2 ]]; then
                error "'ast' requires a <file.mmc> argument"
                echo ""
                show_usage
                exit 1
            fi
            cmd_ast "$2"
            ;;
        tokens)
            if [[ $# -lt 2 ]]; then
                error "'tokens' requires a <file.mmc> argument"
                echo ""
                show_usage
                exit 1
            fi
            cmd_tokens "$2"
            ;;
        c)
            if [[ $# -lt 2 ]]; then
                error "'c' requires a <file.mmc> argument"
                echo ""
                show_usage
                exit 1
            fi
            cmd_c "$2"
            ;;
        self-test)
            cmd_self_test
            ;;
        self-compile)
            cmd_self_compile
            ;;
        version|--version|-v)
            cmd_version
            ;;
        help|--help|-h|"")
            show_usage
            ;;
        *)
            error "Unknown command: ${cmd}"
            echo ""
            show_usage
            exit 1
            ;;
    esac
}

main "$@"
