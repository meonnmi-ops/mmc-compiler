#!/bin/bash
# =================================================================
# MMC Bootstrap Script v2.1 — Phase 7 Daemonization Build
# Self-Hosting: MMC Source -> C Code -> Native Binary
#
# v2.1 Changes:
#   + Fixed: "undefined symbol: main" linker error
#     Library modules (lexer/parser/codegen) compile to .o only
#     Program modules (with main()) compile to executable
#   + Termux $PREFIX/tmp path support for PID/LOG files
#   + clang + lld compatibility (Termux default compiler)
#
# Usage:
#   bash bootstrap.sh                        # Full bootstrap
#   bash bootstrap.sh <file.mmc>             # Single file
#   bash bootstrap.sh --test                 # Run self-test suite
#   bash bootstrap.sh --install              # Install mmc to ~/z/bin/
#   bash bootstrap.sh --detect-arch          # Show CPU/arch flags
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

# Termux support
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

    # Detect compiler (Termux uses clang by default)
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
            # ARMv8-A (Snapdragon, Apple Silicon)
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
            ARCH_VENDOR="ARMv7-A + NEON"
            ;;
        x86_64|amd64)
            if echo "$cpu" | grep -qi "intel"; then
                CFLAGS="${MMC_CFLAGS:-$base_flags -O3 -march=x86-64-v2 -mtune=haswell}"
            else
                CFLAGS="${MMC_CFLAGS:-$base_flags -O3 -march=x86-64-v2 -mtune=znver2}"
            fi
            ARCH_VENDOR="x86_64-v2"
            ;;
        *)
            CFLAGS="${MMC_CFLAGS:-$base_flags -O2}"
            ARCH_VENDOR="Generic (-O2 safe)"
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
# Phase 0: Prerequisites Check
# =================================================================

check_prerequisites() {
    info "Prerequisites စစ်ဆေးနေပါတယ်..."

    if ! command -v python3 &> /dev/null; then
        error "python3 မတွေ့ဘူး။ pkg install python ပြုလုပ်ပါ။"
        exit 1
    fi
    success "python3: $(python3 --version)"

    if ! command -v $CC &> /dev/null; then
        error "$CC မတွေ့ဘူး। pkg install clang ပြုလုပ်ပါ။"
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
        "mmc_lexer.mmc" "mmc_parser.mmc" "mmc_c_codegen.mmc"
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
# Phase 1: Build Runtime Library (.o files only, no linking)
# =================================================================

build_runtime() {
    info "MMC Runtime Library (mmclib) တည်ဆောက်နေပါတယ်..."
    mkdir -p "$BUILD_DIR"

    # Compile to .o (object file only — prevents "undefined symbol: main")
    $CC $CFLAGS -c "$SELFHOSTED_DIR/mmclib.c" \
        -o "$BUILD_DIR/mmclib.o" -I"$SELFHOSTED_DIR" 2>&1
    success "mmclib.o (object file)"

    $CC $CFLAGS -c "$SELFHOSTED_DIR/ia_bridge.c" \
        -o "$BUILD_DIR/ia_bridge.o" -I"$SELFHOSTED_DIR" 2>&1
    success "ia_bridge.o (object file)"
    echo ""
}

# =================================================================
# Compile MMC -> C
# =================================================================

mmc_to_c() {
    local input_file="$1"
    local output_file="$2"

    info "MMC -> C: $(basename "$input_file")"

    python3 "$MMC_HOME/compile_mmc.py" "$input_file" --c 2>&1 | \
        sed '1,/^Generated C Code:/d' | \
        sed '/^===/d' | \
        sed '/^$/d' > "$output_file"

    if [ -s "$output_file" ]; then
        local lines
        lines=$(wc -l < "$output_file")
        success "C code $lines lines ထုတ်ပေးပြီး"
    else
        error "C code ထုတ်လို့မရဘူး: $(basename "$input_file")"
        return 1
    fi
}

# =================================================================
# C -> Binary or .o (auto-detect main())
# =================================================================

c_to_output() {
    local c_file="$1"
    local output_path="$2"

    # Key fix: check if generated C has main() function
    if grep -q 'int main[[:space:]]*(' "$c_file"; then
        # Has main() → compile to executable binary
        info "C -> Binary: $(basename "$c_file")"
        $CC $CFLAGS "$c_file" \
            "$BUILD_DIR/mmclib.o" \
            "$BUILD_DIR/ia_bridge.o" \
            $LDFLAGS \
            -o "$output_path" \
            -I"$SELFHOSTED_DIR" 2>&1

        if [ -f "$output_path" ]; then
            local size
            size=$(ls -lh "$output_path" | awk '{print $5}')
            success "$(basename "$output_path") ($size) binary ဖြစ်ပြီး"
        else
            error "Binary compile မအောင်မြင်ဘူး: $(basename "$c_file")"
            return 1
        fi
    else
        # No main() → compile to object file only (library module)
        local obj_path="${output_path}.o"
        info "C -> Object: $(basename "$c_file") (library module — main() မရှိဘဲ)"
        $CC $CFLAGS -c "$c_file" \
            -o "$obj_path" \
            -I"$SELFHOSTED_DIR" 2>&1

        if [ -f "$obj_path" ]; then
            local size
            size=$(ls -lh "$obj_path" | awk '{print $5}')
            success "$(basename "$obj_path") ($size) object file ဖြစ်ပြီး"
        else
            error "Object compile မအောင်မြင်ဘူး: $(basename "$c_file")"
            return 1
        fi
    fi
}

# =================================================================
# Full Pipeline: MMC -> C -> Binary/.o
# =================================================================

compile_mmc_file() {
    local mmc_file="$1"
    local base_name
    base_name=$(basename "$mmc_file" .mmc)
    local c_file="$BUILD_DIR/${base_name}.c"
    local out_path="$BUILD_DIR/${base_name}"

    info "=== $(basename "$mmc_file") စုံဆောင်းနေပါတယ် ==="
    echo ""

    mmc_to_c "$mmc_file" "$c_file"
    [ $? -eq 0 ] || return 1

    c_to_output "$c_file" "$out_path"
    [ $? -eq 0 ] || return 1

    echo ""
    success "=== $(basename "$mmc_file") DONE ==="
    echo ""
}

# =================================================================
# Bootstrap: Self-Hosted Compiler Modules
# =================================================================

bootstrap() {
    info "=== Bootstrap: Self-Hosting Modules ==="
    echo ""

    # Library modules (no main() — compile to .o only)
    local lib_modules=(
        "mmc_lexer.mmc"
        "mmc_parser.mmc"
        "mmc_c_codegen.mmc"
    )

    local ok_count=0
    local fail_count=0

    for mod in "${lib_modules[@]}"; do
        local mmc_path="$SELFHOSTED_DIR/$mod"
        [ -f "$mmc_path" ] || { warn "$mod မတွေ့ဘူး — skip"; continue; }

        if compile_mmc_file "$mmc_path"; then
            ((ok_count++))
        else
            ((fail_count++))
        fi
    done

    echo ""
    info "=== အကြောင်းကျရေး ==="
    info "အောင်မြင်မှု: $ok_count  | မအောင်မြင်မှု: $fail_count"
    echo ""

    if [ $fail_count -gt 0 ]; then
        warn "အချို့ module တွေ compile မအောင်မြင်ပါ။ ဒါကို့《undefined symbol: main》 ဖြစ်နေတာမဟုတ်ဘဲ object file အဖြစ်သာ တည်ဆောက်ထားတာ ဖြစ်ပါတယ်။"
    fi
}

# =================================================================
# Test Suite
# =================================================================

run_tests() {
    info "=== Test Suite စစ်ဆေးနေပါတယ် ==="
    echo ""

    # Test 1: mmclib self-test (has its own main with MMC_SELFTEST define)
    info "Test 1: mmclib self-test (83 checks)..."
    $CC $CFLAGS -DMMC_RUNTIME_SELFTEST \
        "$SELFHOSTED_DIR/mmclib.c" $LDFLAGS \
        -o "$TMP_DIR/mmc_selftest" -I"$SELFHOSTED_DIR" 2>&1

    "$TMP_DIR/mmc_selftest" 2>&1
    echo ""

    # Test 2: MMC -> C -> Binary pipeline (has main)
    info "Test 2: MMC -> C -> Binary pipeline..."
    echo 'print("MMC Bootstrap Test PASS")' > "$TMP_DIR/test_simple.mmc"

    python3 "$MMC_HOME/compile_mmc.py" "$TMP_DIR/test_simple.mmc" --c 2>&1 | \
        sed '1,/^Generated C Code:/d' | sed '/^===/d' | sed '/^$/d' \
        > "$TMP_DIR/test_simple.c"

    $CC $CFLAGS "$TMP_DIR/test_simple.c" \
        "$BUILD_DIR/mmclib.o" "$BUILD_DIR/ia_bridge.o" \
        $LDFLAGS -o "$TMP_DIR/test_simple_bin" -I"$SELFHOSTED_DIR" 2>&1

    if [ -f "$TMP_DIR/test_simple_bin" ]; then
        local output
        output=$("$TMP_DIR/test_simple_bin" 2>&1)
        if echo "$output" | grep -q "PASS"; then
            success "Pipeline test: PASS"
        else
            error "Pipeline test: FAIL (output မမှန်ကန်ဘူး)"
        fi
    else
        error "Pipeline test: FAIL (compile မအောင်မြင်ဘူး)"
    fi

    # Cleanup
    rm -f "$TMP_DIR/test_simple.mmc" "$TMP_DIR/test_simple.c" "$TMP_DIR/test_simple_bin" "$TMP_DIR/mmc_selftest"

    echo ""
    success "=== အကြောင်းကျရေးကုန် အပြည့်အစုံ ==="
}

# =================================================================
# Install: ~/z/bin/ + PATH
# =================================================================

install_mmc() {
    info "mmc command ကို $INSTALL_DIR/ ထဲသို့ ထည့်သွင်းနေပါတယ်..."
    mkdir -p "$INSTALL_DIR"

    cat > "$INSTALL_DIR/mmc" << WRAPPER
#!/bin/bash
# MMC Compiler CLI v2.1
# Installed to ~/z/bin/ by bootstrap.sh
set -e
MMC_HOME="\${MMC_HOME:-$MMC_HOME}"

case "\${1:-}" in
    compile) shift; python3 "\$MMC_HOME/compile_mmc.py" "\$@" ;;
    run)     shift; python3 "\$MMC_HOME/compile_mmc.py" "\$@" --run ;;
    c)       shift; python3 "\$MMC_HOME/compile_mmc.py" "\$@" --c ;;
    build)   shift; bash "\$MMC_HOME/selfhosted/bootstrap.sh" "\$@" ;;
    test)    bash "\$MMC_HOME/selfhosted/bootstrap.sh" --test ;;
    daemon)  shift; bash "\$MMC_HOME/selfhosted/mmcd.sh" "\$@" ;;
    *)       echo "MMC Compiler v2.1 — MyanOS Self-Hosting
Usage: mmc <command>
  compile|run|c|build|test|daemon start|stop|status" ;;
esac
WRAPPER

    chmod +x "$INSTALL_DIR/mmc" 2>/dev/null
    success "mmc ကို $INSTALL_DIR/mmc ထဲသို့ ထည့်သွင်းပြီး"

    # PATH setup
    if ! echo "$PATH" | grep -q "$INSTALL_DIR"; then
        local marker="# MMC Compiler PATH (bootstrap.sh v2.1)"
        for cfg in "$HOME/.bashrc" "$HOME/.zshrc" "$HOME/.profile"; do
            [ -f "$cfg" ] || continue
            if ! grep -q "MMC Compiler PATH" "$cfg" 2>/dev/null; then
                echo "" >> "$cfg"
                echo "$marker" >> "$cfg"
                echo "export PATH=\"$INSTALL_DIR:\$PATH\"" >> "$cfg"
                success "PATH ကို $(basename "$cfg") ထဲ ထည့်သွင်းပြီး"
            fi
        done
        echo -e "${CYAN}INFO: ဤ session မှာ activate လုပ်ရန်:${NC}"
        echo -e "  ${GREEN}export PATH=\"$INSTALL_DIR:\$PATH\"${NC}"
    else
        success "PATH ထဲတွေ့ပြီး"
    fi
}

# =================================================================
# Main
# =================================================================

main() {
    echo ""
    echo "============================================================"
    echo "  MMC Bootstrap v2.1 — Phase 7 Daemonization"
    echo "  MMC -> C -> Binary (.o / executable auto-detect)"
    echo "============================================================"
    echo ""
    info "MMC_HOME    : $MMC_HOME"
    info "Compiler    : $CC"
    info "CFLAGS      : $CFLAGS"
    info "Arch        : $ARCH_NAME ($ARCH_VENDOR)"
    info "Build Dir   : $BUILD_DIR"
    echo ""

    case "${1:-full}" in
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
            info "CC  : $CC"
            info "Flags: $CFLAGS"
            exit 0
            ;;
        --help|-h)
            echo "MMC Bootstrap v2.1"
            echo "  bash bootstrap.sh              Full bootstrap"
            echo "  bash bootstrap.sh <file.mmc>   Single file"
            echo "  bash bootstrap.sh --test        Run tests"
            echo "  bash bootstrap.sh --install     Install mmc CLI"
            echo "  bash bootstrap.sh --detect-arch Show arch flags"
            echo "  bash bootstrap.sh --daemon start|stop|status"
            exit 0
            ;;
    esac

    # Full bootstrap or single file
    check_prerequisites
    build_runtime

    if [ -n "${1:-}" ] && [ -f "${1:-}" ]; then
        compile_mmc_file "$1"
    else
        bootstrap
    fi

    info "Bootstrap ပြီးပါပြီ။"
    echo ""
}

main "$@"
