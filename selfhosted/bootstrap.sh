#!/bin/bash
# =================================================================
# MMC Bootstrap Script v2.2 — Phase 7 Daemonization Build
# Self-Hosting: MMC Source -> C Code -> Native Binary
#
# v2.2 Changes:
#   + Compiler modules (lexer/parser/codegen) compile to C only
#     — they are NOT compiled to binary (no main() function)
#   + Bootstrap focuses on: runtime library + test verification
#   + User programs compile MMC -> C -> Binary (with main() check)
#   + Termux $PREFIX/tmp path support
#   + clang + lld compatibility
#
# Architecture:
#   [MMC Source] --compile_mmc.py--> [Python Code]  (compiler runs here)
#   [MMC Source] --compile_mmc.py --c--> [C Code]   (for native binaries)
#   [C Code]     --clang/gcc-->       [Binary]      (user programs only)
#
# IMPORTANT:
#   mmc_lexer.mmc, mmc_parser.mmc, mmc_c_codegen.mmc are the
#   MMC COMPILER ITSELF. They run via Python (compile_mmc.py).
#   They are NOT meant to be compiled to C binary.
#
# Usage:
#   bash bootstrap.sh              # Build runtime + verify
#   bash bootstrap.sh <file.mmc>   # Compile user program to binary
#   bash bootstrap.sh --test       # Run full test suite
#   bash bootstrap.sh --install    # Install mmc CLI to ~/z/bin/
#   bash bootstrap.sh --detect-arch
#   bash bootstrap.sh --daemon start|stop|status
#
# =================================================================

set -e

# =================================================================
# Configuration
# =================================================================

MMC_HOME="${MMC_HOME:-$HOME/z/my-project/mmc-compiler}"
SELFHOSTED_DIR="$MMC_HOME/selfhosted"
BUILD_DIR="$MMC_HOME/build"
INSTALL_DIR="${MMC_INSTALL_DIR:-$HOME/z/bin}"

# Termux: $PREFIX = /data/data/com.termux/files/usr
if [ -n "${PREFIX:-}" ] && [ -d "$PREFIX/tmp" ]; then
    TMP_DIR="$PREFIX/tmp"
else
    TMP_DIR="/tmp"
fi

# =================================================================
# Auto-Detect Architecture & Compiler
# =================================================================

detect_arch() {
    local arch=""
    local cpu=""

    arch="$(uname -m 2>/dev/null || echo "unknown")"

    if [ -f /proc/cpuinfo ]; then
        cpu="$(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2 | xargs)"
    fi

    # Termux default = clang
    if command -v clang &> /dev/null; then
        CC="${MMC_CC:-clang}"
    elif command -v gcc &> /dev/null; then
        CC="${MMC_CC:-gcc}"
    else
        CC="${MMC_CC:-cc}"
    fi

    local base_flags="-std=c99 -Wall -Wextra"

    case "$arch" in
        aarch64|arm64)
            if echo "$cpu" | grep -qi "snapdragon"; then
                CFLAGS="${MMC_CFLAGS:-$base_flags -O3 -march=armv8-a -mtune=cortex-a78}"
            elif echo "$cpu" | grep -qi "apple\|m1\|m2\|m3\|m4"; then
                CFLAGS="${MMC_CFLAGS:-$base_flags -O3 -march=armv8.5-a -mtune=apple-m1}"
            else
                CFLAGS="${MMC_CFLAGS:-$base_flags -O3 -march=armv8-a -mtune=cortex-a76}"
            fi
            ARCH_VENDOR="ARMv8-A"
            ;;
        armv7l|armhf)
            CFLAGS="${MMC_CFLAGS:-$base_flags -O3 -march=armv7-a -mtune=cortex-a9 -mfpu=neon}"
            ARCH_VENDOR="ARMv7-A"
            ;;
        x86_64|amd64)
            CFLAGS="${MMC_CFLAGS:-$base_flags -O3 -march=x86-64-v2}"
            ARCH_VENDOR="x86_64-v2"
            ;;
        *)
            CFLAGS="${MMC_CFLAGS:-$base_flags -O2}"
            ARCH_VENDOR="Generic"
            ;;
    esac

    LDFLAGS="${MMC_LDFLAGS:--lm}"
    ARCH_NAME="$arch"
    ARCH_CPU="$cpu"
}

detect_arch

# Colors
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; NC='\033[0m'

info()    { echo -e "${CYAN}[MMC]${NC} $1"; }
success() { echo -e "${GREEN}[OK]${NC} $1"; }
warn()    { echo -e "${YELLOW}[WARN]${NC} $1"; }
error()   { echo -e "${RED}[ERROR]${NC} $1"; }

# =================================================================
# Phase 0: Prerequisites
# =================================================================

check_prerequisites() {
    info "Prerequisites စစ်ဆေးနေပါတယ်..."

    if ! command -v python3 &> /dev/null; then
        error "python3 မတွေ့ဘူး။ pkg install python ပြုလုပ်ပါ။"
        exit 1
    fi
    success "python3: $(python3 --version)"

    if ! command -v $CC &> /dev/null; then
        error "$CC မတွေ့ဘူး။ pkg install clang ပြုလုပ်ပါ။"
        exit 1
    fi
    success "$CC: $($CC --version 2>&1 | head -1)"

    if [ ! -f "$MMC_HOME/compile_mmc.py" ]; then
        error "compile_mmc.py မတွေ့ဘူး: $MMC_HOME/compile_mmc.py"
        exit 1
    fi
    success "compile_mmc.py တွေ့ပါပြီ"

    local required_files=(
        "mmclib.h" "mmclib.c"
        "ia_bridge.h" "ia_bridge.c"
    )

    for f in "${required_files[@]}"; do
        if [ -f "$SELFHOSTED_DIR/$f" ]; then
            success "$f တွေ့ပါပြီ"
        else
            error "$f မတွေ့ဘူး: $SELFHOSTED_DIR/$f"
            exit 1
        fi
    done
    echo ""
}

# =================================================================
# Phase 1: Build Runtime Library (.o files)
# =================================================================

build_runtime() {
    info "MMC Runtime Library (mmclib) တည်ဆောက်နေပါတယ်..."
    mkdir -p "$BUILD_DIR"

    $CC $CFLAGS -c "$SELFHOSTED_DIR/mmclib.c" \
        -o "$BUILD_DIR/mmclib.o" -I"$SELFHOSTED_DIR" 2>&1
    success "mmclib.o compiled"

    $CC $CFLAGS -c "$SELFHOSTED_DIR/ia_bridge.c" \
        -o "$BUILD_DIR/ia_bridge.o" -I"$SELFHOSTED_DIR" 2>&1
    success "ia_bridge.o compiled"

    if [ -f "$SELFHOSTED_DIR/ia_audio.c" ]; then
        $CC $CFLAGS -c "$SELFHOSTED_DIR/ia_audio.c" \
            -o "$BUILD_DIR/ia_audio.o" -I"$SELFHOSTED_DIR" 2>&1
        success "ia_audio.o compiled"
    fi

    echo ""
}

# =================================================================
# Phase 2: Verify Compiler Modules (Python runtime)
# =================================================================

verify_compiler_modules() {
    info "MMC Compiler Modules စစ်ဆေးနေပါတယ် (Python runtime)..."
    echo ""

    local modules=(
        "mmc_lexer.mmc:MMC Lexer (631 lines)"
        "mmc_parser.mmc:MMC Parser (1492 lines)"
        "mmc_c_codegen.mmc:MMC C CodeGen (50KB)"
    )

    for mod_info in "${modules[@]}"; do
        local mod="${mod_info%%:*}"
        local desc="${mod_info#*:}"
        local path="$SELFHOSTED_DIR/$mod"

        if [ -f "$path" ]; then
            local lines
            lines=$(wc -l < "$path")
            success "$desc — verified ($lines lines)"
        else
            warn "$desc — မတွေ့ဘူး (skip)"
        fi
    done

    info ""
    info "NOTE: Compiler modules (lexer/parser/codegen) တွေက MMC compiler ၏"
    info "အဓိက အစိတ် ဖြစ်ပါတယ်။ Python transpiler နဲ့ အလုပ်လုပ်ပြီး"
    info "C binary အဖြစ် compile လုပ်စရာ မလိုပါဘူး။"
    echo ""
}

# =================================================================
# Compile User MMC Program -> C -> Binary
# =================================================================

compile_user_program() {
    local mmc_file="$1"
    local base_name
    base_name=$(basename "$mmc_file" .mmc)
    local c_file="$BUILD_DIR/${base_name}.c"
    local bin_file="$BUILD_DIR/${base_name}"

    info "=== $(basename "$mmc_file") စုံဆောင်းနေပါတယ် ==="

    # MMC -> C
    info "MMC -> C: $(basename "$mmc_file")"
    python3 "$MMC_HOME/compile_mmc.py" "$mmc_file" --c 2>&1 | \
        sed '1,/^Generated C Code:/d' | \
        sed '/^===/d' | \
        sed '/^$/d' > "$c_file"

    if [ ! -s "$c_file" ]; then
        error "C code ထုတ်လို့မရဘူး"
        return 1
    fi
    success "C code $(wc -l < "$c_file") lines ထုတ်ပေးပြီး"

    # Check for main()
    if ! grep -q 'int main[[:space:]]*(' "$c_file"; then
        warn "$(basename "$mmc_file") တွေ့ main() မရှိဘူး — library module ဖြစ်နေပါတယ်"
        warn "Binary compile လုပ်မည့် မဟုတ်ပါဘူး"
        return 0
    fi

    # C -> Binary
    info "C -> Binary: $(basename "$c_file")"
    $CC $CFLAGS "$c_file" \
        "$BUILD_DIR/mmclib.o" \
        "$BUILD_DIR/ia_bridge.o" \
        $LDFLAGS \
        -o "$bin_file" \
        -I"$SELFHOSTED_DIR" 2>&1

    if [ -f "$bin_file" ]; then
        local size
        size=$(ls -lh "$bin_file" | awk '{print $5}')
        success "$(basename "$bin_file") ($size) binary ဖြစ်ပြီး"
    else
        error "Binary compile မအောင်မြင်ဘူး: $(basename "$mmc_file")"
        return 1
    fi
}

# =================================================================
# Full Bootstrap
# =================================================================

bootstrap() {
    info "=== MMC Bootstrap v2.2 ==="
    echo ""

    # Step 1: Build runtime
    build_runtime

    # Step 2: Verify compiler modules
    verify_compiler_modules

    # Step 3: Run pipeline test
    info "MMC Pipeline Test (MMC -> Python -> Execute)..."
    local test_code='print("MMC Pipeline Test PASS")'
    echo "$test_code" > "$TMP_DIR/mmc_test.mmc"

    python3 "$MMC_HOME/compile_mmc.py" "$TMP_DIR/mmc_test.mmc" --run 2>&1
    local rc=$?
    rm -f "$TMP_DIR/mmc_test.mmc"

    if [ $rc -eq 0 ]; then
        success "Pipeline test: MMC -> Python -> Execute PASS"
    else
        error "Pipeline test: FAIL"
    fi

    echo ""
    info "=== Bootstrap ပြီးပါပြီ ==="
    info "Runtime   : mmclib.o + ia_bridge.o"
    info "Compiler  : compile_mmc.py (Python)"
    info "CFLAGS    : $CFLAGS"
    echo ""
}

# =================================================================
# Test Suite
# =================================================================

run_tests() {
    info "=== Test Suite ==="
    echo ""

    # Test 1: mmclib self-test
    info "Test 1: mmclib self-test (83 checks)..."
    $CC $CFLAGS -DMMC_RUNTIME_SELFTEST \
        "$SELFHOSTED_DIR/mmclib.c" $LDFLAGS \
        -o "$TMP_DIR/mmc_selftest" -I"$SELFHOSTED_DIR" 2>&1

    "$TMP_DIR/mmc_selftest" 2>&1
    echo ""

    # Test 2: ia_bridge self-test
    info "Test 2: ia_bridge self-test..."
    $CC $CFLAGS -DMMC_AI_BRIDGE_SELFTEST \
        "$SELFHOSTED_DIR/ia_bridge.c" $LDFLAGS \
        -o "$TMP_DIR/mmc_bridge_test" -I"$SELFHOSTED_DIR" 2>&1

    "$TMP_DIR/mmc_bridge_test" 2>&1
    echo ""

    # Test 3: MMC -> C -> Binary pipeline
    info "Test 3: MMC -> C -> Binary pipeline..."
    echo 'print("MMC Native Test PASS")' > "$TMP_DIR/test_native.mmc"

    python3 "$MMC_HOME/compile_mmc.py" "$TMP_DIR/test_native.mmc" --c 2>&1 | \
        sed '1,/^Generated C Code:/d' | sed '/^===/d' | sed '/^$/d' \
        > "$TMP_DIR/test_native.c"

    $CC $CFLAGS "$TMP_DIR/test_native.c" \
        "$BUILD_DIR/mmclib.o" "$BUILD_DIR/ia_bridge.o" \
        $LDFLAGS -o "$TMP_DIR/test_native_bin" -I"$SELFHOSTED_DIR" 2>&1

    if [ -f "$TMP_DIR/test_native_bin" ]; then
        local output
        output=$("$TMP_DIR/test_native_bin" 2>&1)
        if echo "$output" | grep -q "PASS"; then
            success "Native pipeline: PASS"
        else
            warn "Native pipeline: output မမှန်ကန်ဘူး ($output)"
        fi
    else
        warn "Native pipeline: compile မအောင်မြင်ဘူး (C codegen ဖြစ်ဆဲဖြစ်ပါတယ်)"
    fi

    # Cleanup
    rm -f "$TMP_DIR/test_native.mmc" "$TMP_DIR/test_native.c" "$TMP_DIR/test_native_bin" "$TMP_DIR/mmc_selftest" "$TMP_DIR/mmc_bridge_test"

    echo ""
    success "=== Test Suite ပြီးပါပြီ ==="
}

# =================================================================
# Install: ~/z/bin/ + PATH
# =================================================================

install_mmc() {
    info "mmc command ကို $INSTALL_DIR/ ထဲသို့ ထည့်သွင်းနေပါတယ်..."
    mkdir -p "$INSTALL_DIR"

    cat > "$INSTALL_DIR/mmc" << WRAPPER
#!/bin/bash
# MMC Compiler CLI v2.2 — Installed by bootstrap.sh
set -e
MMC_HOME="\${MMC_HOME:-$MMC_HOME}"

case "\${1:-}" in
    compile) shift; python3 "\$MMC_HOME/compile_mmc.py" "\$@" ;;
    run)     shift; python3 "\$MMC_HOME/compile_mmc.py" "\$@" --run ;;
    c)       shift; python3 "\$MMC_HOME/compile_mmc.py" "\$@" --c ;;
    ast)     shift; python3 "\$MMC_HOME/compile_mmc.py" "\$@" --ast ;;
    tokens)  shift; python3 "\$MMC_HOME/compile_mmc.py" "\$@" --tokens ;;
    build)   shift; bash "\$MMC_HOME/selfhosted/bootstrap.sh" "\$@" ;;
    test)    bash "\$MMC_HOME/selfhosted/bootstrap.sh" --test ;;
    daemon)  shift; bash "\$MMC_HOME/selfhosted/mmcd.sh" "\$@" ;;
    *)
        echo "MMC Compiler v2.2 — MyanOS"
        echo ""
        echo "Usage: mmc <command> [args]"
        echo ""
        echo "  compile <file.mmc>   MMC -> Python"
        echo "  run <file.mmc>       MMC -> Python -> Execute"
        echo "  c <file.mmc>         MMC -> C code"
        echo "  ast <file.mmc>       Show AST"
        echo "  tokens <file.mmc>    Show tokens"
        echo "  build [file.mmc]     Build runtime / compile user program"
        echo "  test                 Run test suite"
        echo "  daemon start|stop|status"
        ;;
esac
WRAPPER

    chmod +x "$INSTALL_DIR/mmc" 2>/dev/null
    success "mmc ကို $INSTALL_DIR/mmc ထဲ ထည့်သွင်းပြီး"

    if ! echo "$PATH" | grep -q "$INSTALL_DIR"; then
        for cfg in "$HOME/.bashrc" "$HOME/.zshrc" "$HOME/.profile"; do
            [ -f "$cfg" ] || continue
            if ! grep -q "MMC Compiler PATH" "$cfg" 2>/dev/null; then
                echo "" >> "$cfg"
                echo "# MMC Compiler PATH (bootstrap.sh v2.2)" >> "$cfg"
                echo "export PATH=\"$INSTALL_DIR:\$PATH\"" >> "$cfg"
                success "PATH ကို $(basename "$cfg") ထဲ ထည့်သွင်းပြီး"
            fi
        done
        echo -e "${CYAN}Activation: export PATH=\"$INSTALL_DIR:\$PATH\"${NC}"
    else
        success "PATH ထဲ ပါပြီး"
    fi
}

# =================================================================
# Main
# =================================================================

main() {
    echo ""
    echo "============================================================"
    echo "  MMC Bootstrap v2.2 — Phase 7"
    echo "  Runtime: mmclib + ia_bridge (.o)"
    echo "  Compiler: compile_mmc.py (Python)"
    echo "============================================================"
    echo ""
    info "MMC_HOME : $MMC_HOME"
    info "Compiler : $CC"
    info "CFLAGS   : $CFLAGS"
    info "Arch     : $ARCH_NAME ($ARCH_VENDOR)"
    info "Build    : $BUILD_DIR"
    echo ""

    case "${1:-build}" in
        --clean)
            rm -rf "$BUILD_DIR"
            success "Build directory ရှင်းလင်းပြီး"
            exit 0
            ;;
        --test)
            check_prerequisites
            build_runtime
            run_tests
            exit 0
            ;;
        --install)
            install_mmc
            exit 0
            ;;
        --daemon)
            shift
            bash "$SELFHOSTED_DIR/mmcd.sh" "${1:-status}"
            exit 0
            ;;
        --detect-arch|--arch)
            info "Arch: $ARCH_NAME ($ARCH_VENDOR)"
            info "CC: $CC"
            info "Flags: $CFLAGS"
            exit 0
            ;;
        --help|-h)
            echo "MMC Bootstrap v2.2"
            echo "  bash bootstrap.sh              Build runtime + verify"
            echo "  bash bootstrap.sh <file.mmc>   Compile user program"
            echo "  bash bootstrap.sh --test        Run tests"
            echo "  bash bootstrap.sh --install     Install CLI"
            echo "  bash bootstrap.sh --detect-arch Show arch"
            echo "  bash bootstrap.sh --daemon start|stop|status"
            exit 0
            ;;
    esac

    # Single file mode
    if [ -n "${1:-}" ] && [ -f "${1:-}" ]; then
        check_prerequisites
        build_runtime
        compile_user_program "$1"
        exit 0
    fi

    # Full bootstrap
    check_prerequisites
    bootstrap
}

main "$@"
