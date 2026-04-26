#!/bin/bash
# NYANLIN Binary AI v3.0 - Build Script
# Pure C GGUF inference engine - no llama.cpp dependency

set -e

echo "=== NYANLIN Binary AI v3.0 Build ==="
echo ""

# Check for gcc
if command -v gcc &>/dev/null; then
    CC=gcc
elif command -v cc &>/dev/null; then
    CC=cc
else
    echo "ERROR: No C compiler found. Install gcc:"
    echo "  Debian/Ubuntu: sudo apt-get install build-essential"
    echo "  Termux:       pkg install clang"
    exit 1
fi

echo "[1/2] Compiling nyanlin.c ..."
$CC -O3 -Wall -o nyanlin nyanlin.c -lm

echo "[2/2] Build complete!"
echo ""
echo "Binary: $(ls -lh nyanlin | awk '{print $5}')"
echo ""
echo "Usage:"
echo "  ./nyanlin <model.gguf>              # Interactive mode"
echo "  ./nyanlin <model.gguf> \"prompt\"     # Single prompt mode"
echo ""
echo "Supported models: LLaMA, Qwen2 (GGUF v3, Q4_0/Q4_1/Q8_0/F16/F32)"
echo ""
echo "Good luck, Nyanlin! 🚀"
