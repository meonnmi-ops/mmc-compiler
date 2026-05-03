#!/bin/bash
# ===================================================
# MMC Compiler v8.2 - Build & Run Script
# ===================================================
# For Termux / Android (ARM64)
# Usage: ./mmc_build.sh [file.mmc]
# ===================================================

set -e

MMC_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="$MMC_DIR/src"
TEST_DIR="$MMC_DIR/tests"

echo "========================================="
echo " MMC Compiler v8.2 - Build & Run"
echo "========================================="
echo ""

# Check dependencies
check_dep() {
    if ! command -v "$1" &> /dev/null; then
        echo "Missing: $1"
        echo "Install: $2"
        return 1
    fi
    echo "Found: $1 ($(command -v "$1"))"
}

check_dep python3 "pkg install python"
check_dep llc "pkg install llvm"
check_dep gcc "pkg install clang" || check_dep clang "pkg install clang"

echo ""
echo "--- Compiling MMC ---"

if [ -z "$1" ]; then
    echo "Usage: $0 <file.mmc> [--run]"
    echo ""
    echo "Test files:"
    for f in "$TEST_DIR"/*.mmc; do
        echo "  $f"
    done
    exit 0
fi

INPUT="$1"
RUN_FLAG=""

if [ "$2" = "--run" ] || [ "$2" = "-r" ]; then
    RUN_FLAG="--run"
fi

# Compile
python3 "$SRC_DIR/mmc.py" compile "$INPUT" -v $RUN_FLAG

echo ""
echo "Done!"
