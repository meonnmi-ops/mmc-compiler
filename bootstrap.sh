#!/bin/bash
# =================================================================
# MMC Bootstrap Script v1.0
# Self-Hosting: MMC Source -> C Code -> Native Binary
#
# Usage:
#   bash bootstrap.sh                        # Full bootstrap (all .mmc files)
#   bash bootstrap.sh <file.mmc>             # Single file
#   bash bootstrap.sh --clean                # Clean build artifacts
#   bash bootstrap.sh --test                 # Run self-test suite
#   bash bootstrap.sh --install              # Install mmc command
#
# Environment Variables:
#   MMC_HOME    - MMC compiler directory (default: ~/z/my-project/mmc-compiler)
#   MMC_CC      - C compiler (default: gcc)
#   MMC_CFLAGS  - Extra compiler flags (default: -std=c99 -Wall -Wextra -O2)
#   MMC_LDFLAGS - Extra linker flags (default: -lm)
#
# Path: ~/z/my-project/mmc-compiler/selfhosted/
# =================================================================

set -e

# =================================================================
# Configuration
# =================================================================

MMC_HOME="${MMC_HOME:-/home/z/my-project/mmc-compiler}"
SELFHOSTED_DIR="$MMC_HOME/selfhosted"
BUILD_DIR="$MMC_HOME/build"
CC="${MMC_CC:-gcc}"
CFLAGS="${MMC_CFLAGS:--std=c99 -Wall -Wextra -O2}"
LDFLAGS="${MMC_LDFLAGS:--lm}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'  # No Color

# =================================================================
# Utility Functions
# =================================================================

info() {
    echo -e "${CYAN}[MMC Bootstrap]${NC} $1"
}

success() {
    echo -e "${GREEN}[OK]${NC} $1"
}

warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_header() {
    echo ""
    echo "============================================================"
    echo "  MMC Bootstrap Compiler v1.0"
    echo "  Self-Hosting: MMC -> C -> Native Binary"
    echo "============================================================"
    echo ""
    info "MMC_HOME   : $MMC_HOME"
    info "Compiler   : $CC"
    info "CFLAGS     : $CFLAGS"
    info "Build Dir  : $BUILD_DIR"
    echo ""
}

# =================================================================
# Phase 0: Prerequisites Check
# =================================================================

check_prerequisites() {
    info "Checking prerequisites..."

    # Check Python 3
    if ! command -v python3 &> /dev/null; then
        error "python3 not found. Install Python 3 to continue."
        exit 1
    fi
    success "python3: $(python3 --version)"

    # Check C compiler
    if ! command -v $CC &> /dev/null; then
        error "$CC not found. Install gcc or set MMC_CC."
        exit 1
    fi
    success "$CC: $($CC --version | head -1)"

    # Check MMC compiler files
    if [ ! -f "$MMC_HOME/compile_mmc.py" ]; then
        error "compile_mmc.py not found at $MMC_HOME/compile_mmc.py"
        exit 1
    fi
    success "compile_mmc.py found"

    # Check selfhosted files
    local required_files=(
        "mmclib.h"
        "mmclib.c"
        "ia_bridge.h"
        "ia_bridge.c"
        "mmc_lexer.mmc"
        "mmc_parser.mmc"
        "mmc_c_codegen.mmc"
    )

    for f in "${required_files[@]}"; do
        if [ -f "$SELFHOSTED_DIR/$f" ]; then
            success "$f found"
        else
            error "$f not found at $SELFHOSTED_DIR/$f"
            exit 1
        fi
    done

    echo ""
}

# =================================================================
# Phase 1: Build MMC Runtime Library (mmclib)
# =================================================================

build_runtime() {
    info "Building MMC Runtime Library (mmclib)..."

    mkdir -p "$BUILD_DIR"

    # Compile mmclib.c to object file
    $CC $CFLAGS -c "$SELFHOSTED_DIR/mmclib.c" -o "$BUILD_DIR/mmclib.o" -I"$SELFHOSTED_DIR"

    # Compile ia_bridge.c to object file
    $CC $CFLAGS -c "$SELFHOSTED_DIR/ia_bridge.c" -o "$BUILD_DIR/ia_bridge.o" -I"$SELFHOSTED_DIR"

    success "mmclib.o compiled"
    success "ia_bridge.o compiled"
    echo ""
}

# =================================================================
# Phase 2: Compile MMC to C
# =================================================================

mmc_to_c() {
    local input_file="$1"
    local output_file="$2"

    info "Compiling MMC -> C: $(basename $input_file)"

    python3 "$MMC_HOME/compile_mmc.py" "$input_file" --c 2>&1 | \
        sed '1,/^Generated C Code:/d' | \
        sed '/^===/d' | \
        sed '/^$/d' > "$output_file"

    if [ -s "$output_file" ]; then
        local lines=$(wc -l < "$output_file")
        success "Generated $lines lines of C code"
    else
        error "Failed to generate C code from $input_file"
        return 1
    fi
}

# =================================================================
# Phase 3: Compile C to Binary
# =================================================================

c_to_binary() {
    local c_file="$1"
    local output_bin="$2"

    info "Compiling C -> Binary: $(basename $c_file)"

    $CC $CFLAGS "$c_file" \
        "$BUILD_DIR/mmclib.o" \
        "$BUILD_DIR/ia_bridge.o" \
        $LDFLAGS \
        -o "$output_bin" \
        -I"$SELFHOSTED_DIR" 2>&1

    if [ -f "$output_bin" ]; then
        local size=$(ls -lh "$output_bin" | awk '{print $5}')
        success "Binary created: $(basename $output_bin) ($size)"
    else
        error "Failed to compile $c_file"
        return 1
    fi
}

# =================================================================
# Full Pipeline: MMC -> C -> Binary
# =================================================================

compile_mmc_file() {
    local mmc_file="$1"
    local base_name=$(basename "$mmc_file" .mmc)
    local c_file="$BUILD_DIR/${base_name}.c"
    local bin_file="$BUILD_DIR/${base_name}"

    info "=== Processing: $(basename $mmc_file) ==="
    echo ""

    # Step 1: MMC -> C
    mmc_to_c "$mmc_file" "$c_file"
    if [ $? -ne 0 ]; then return 1; fi

    # Step 2: C -> Binary
    c_to_binary "$c_file" "$bin_file"
    if [ $? -ne 0 ]; then return 1; fi

    echo ""
    success "=== $(basename $mmc_file) -> $(basename $bin_file) DONE ==="
    echo ""

    # Return the binary path
    echo "$bin_file"
}

# =================================================================
# Bootstrap: Compile Self-Hosted Modules
# =================================================================

bootstrap() {
    info "=== Bootstrap Phase: Self-Hosting Modules ==="
    echo ""

    local modules=(
        "mmc_lexer.mmc"
        "mmc_parser.mmc"
        "mmc_c_codegen.mmc"
    )

    local success_count=0
    local fail_count=0
    local results=()

    for mod in "${modules[@]}"; do
        local mmc_path="$SELFHOSTED_DIR/$mod"

        if [ ! -f "$mmc_path" ]; then
            warn "Skipping $mod (not found)"
            continue
        fi

        local bin_path=$(compile_mmc_file "$mmc_path")
        if [ $? -eq 0 ]; then
            results+=("  $GREEN->$NC $(basename $mod) -> $(basename $bin_path)")
            ((success_count++))
        else
            results+=("  $RED X $NC $(basename $mod) FAILED")
            ((fail_count++))
        fi
    done

    echo ""
    info "=== Bootstrap Results ==="
    for r in "${results[@]}"; do
        echo -e "  $r"
    done
    echo ""
    info "Success: $success_count  Failed: $fail_count"
    echo ""
}

# =================================================================
# Test Suite
# =================================================================

run_tests() {
    info "=== Running Test Suite ==="
    echo ""

    # Test 1: mmclib self-test
    info "Test 1: mmclib self-test (83 checks)..."
    $CC $CFLAGS -DMMC_RUNTIME_SELFTEST \
        "$SELFHOSTED_DIR/mmclib.c" $LDFLAGS \
        -o /tmp/mmc_selftest -I"$SELFHOSTED_DIR"

    /tmp/mmc_selftest 2>&1
    echo ""

    echo ""

    # Test 3: Simple MMC program
    info "Test 3: MMC -> C -> Binary pipeline..."
    local test_code='print("MMC Bootstrap Test PASS")'
    echo "$test_code" > /tmp/test_simple.mmc

    python3 "$MMC_HOME/compile_mmc.py" /tmp/test_simple.mmc --c 2>&1 | \
        sed '1,/^Generated C Code:/d' | sed '/^===/d' | sed '/^$/d' \
        > /tmp/test_simple.c

    $CC $CFLAGS /tmp/test_simple.c \
        "$BUILD_DIR/mmclib.o" "$BUILD_DIR/ia_bridge.o" \
        $LDFLAGS -o /tmp/test_simple_bin -I"$SELFHOSTED_DIR" 2>&1

    if [ -f /tmp/test_simple_bin ]; then
        local output=$(/tmp/test_simple_bin 2>&1)
        if echo "$output" | grep -q "PASS"; then
            success "Pipeline test: PASS"
        else
            error "Pipeline test: FAIL (unexpected output)"
        fi
    else
        error "Pipeline test: FAIL (compilation error)"
    fi

    echo ""
    success "=== All Tests Complete ==="
}

# =================================================================
# Install Command
# =================================================================

install_mmc() {
    info "Installing MMC compiler command..."
    local install_dir="$HOME/.local/bin"
    mkdir -p "$install_dir"

    # Create mmc wrapper script
    cat > "$install_dir/mmc" << 'WRAPPER'
#!/bin/bash
# MMC Compiler CLI Wrapper
# Installed by bootstrap.sh

set -e
MMC_HOME="${MMC_HOME:-/home/z/my-project/mmc-compiler}"

case "${1:-}" in
    compile)
        shift
        python3 "$MMC_HOME/compile_mmc.py" "$@" 2>&1
        ;;
    run)
        shift
        python3 "$MMC_HOME/compile_mmc.py" "$@" --run 2>&1
        ;;
    c)
        shift
        python3 "$MMC_HOME/compile_mmc.py" "$@" --c 2>&1
        ;;
    ast)
        shift
        python3 "$MMC_HOME/compile_mmc.py" "$@" --ast 2>&1
        ;;
    tokens)
        shift
        python3 "$MMC_HOME/compile_mmc.py" "$@" --tokens 2>&1
        ;;
    bootstrap)
        bash "$MMC_HOME/selfhosted/bootstrap.sh"
        ;;
    test)
        bash "$MMC_HOME/selfhosted/bootstrap.sh" --test
        ;;
    *)
        echo "MMC Compiler v4.0 - MyanOS Self-Hosting"
        echo ""
        echo "Usage: mmc <command> [options]"
        echo ""
        echo "Commands:"
        echo "  compile <file.mmc>  Compile MMC to Python"
        echo "  run <file.mmc>      Compile and execute"
        echo "  c <file.mmc>        Compile MMC to C code"
        echo "  ast <file.mmc>      Show AST"
        echo "  tokens <file.mmc>   Show token stream"
        echo "  bootstrap           Run full self-hosting bootstrap"
        echo "  test                Run test suite"
        echo ""
        echo "Environment:"
        echo "  MMC_HOME  MMC compiler directory"
        echo "  MMC_CC    C compiler (default: gcc)"
        ;;
esac
WRAPPER

    chmod +x "$install_dir/mmc"
    success "mmc command installed to $install_dir/mmc"

    if ! echo "$PATH" | grep -q "$install_dir"; then
        warn "Add $install_dir to your PATH:"
        echo '  export PATH="$HOME/.local/bin:$PATH"'
    fi
}

# =================================================================
# Clean
# =================================================================

clean_build() {
    info "Cleaning build artifacts..."
    rm -rf "$BUILD_DIR"
    success "Build directory cleaned"
}

# =================================================================
# Main
# =================================================================

main() {
    case "${1:-full}" in
        --clean)
            clean_build
            exit 0
            ;;
        --test)
            print_header
            check_prerequisites
            build_runtime
            run_tests
            exit 0
            ;;
        --install)
            install_mmc
            exit 0
            ;;
        --help|-h)
            echo "MMC Bootstrap Script v1.0"
            echo "Usage: bash bootstrap.sh [command]"
            echo ""
            echo "Commands:"
            echo "  (none)           Full bootstrap (all self-hosted modules)"
            echo "  <file.mmc>       Compile single MMC file to binary"
            echo "  --clean          Clean build artifacts"
            echo "  --test           Run test suite"
            echo "  --install        Install 'mmc' CLI command"
            echo "  --help           Show this help"
            exit 0
            ;;
    esac

    print_header

    # Phase 0: Check prerequisites
    check_prerequisites

    # Phase 1: Build runtime
    build_runtime

    if [ -n "${1:-}" ] && [ -f "${1:-}" ]; then
        # Single file mode
        compile_mmc_file "$1"
    else
        # Full bootstrap
        bootstrap
    fi

    info "Bootstrap complete."
    echo ""
}

main "$@"
