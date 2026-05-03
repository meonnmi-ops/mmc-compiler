#!/usr/bin/env python3
"""
MMC Compiler - Main Driver
===========================
Unified compiler driver for MMC Programming Language.
Supports multiple backends: LLVM IR, C, Python transpilation.

Version: 8.2.0
License: MIT
Author: MMC Compiler Team / Nyanlin-AI

Usage:
    python3 mmc.py compile hello.mmc              # Compile to LLVM IR
    python3 mmc.py compile hello.mmc --run         # Compile and run
    python3 mmc.py compile hello.mmc --backend=c   # Compile to C
    python3 mmc.py run hello.mmc                   # Short form: compile + run
    python3 mmc.py tokenize hello.mmc              # Show tokens
    python3 mmc.py ast hello.mmc                   # Show AST
    python3 mmc.py version                         # Show version info
"""

import sys
import os
import subprocess
import argparse
import time

# Add src directory to path
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from mmc_lexer import tokenize, MMCLexer, TokenType
from mmc_parser import parse, print_ast, MMCParser
from mmc_codegen_llvm import compile_to_llvm, MMCCodegenLLVM


MMC_VERSION = "8.2.0"
MMC_VERSION_MAJOR = 8
MMC_VERSION_MINOR = 2
MMC_VERSION_PATCH = 0


def cmd_compile(args):
    """Compile MMC source to LLVM IR and optionally run it."""
    input_path = args.input
    output_path = args.output
    backend = args.backend or 'llvm'
    run_after = args.run
    verbose = args.verbose
    optimize = args.opt or 0

    if not os.path.exists(input_path):
        print(f"Error: File not found: {input_path}")
        return 1

    with open(input_path, 'r', encoding='utf-8') as f:
        source = f.read()

    if verbose:
        print(f"=== MMC Compiler v{MMC_VERSION} ===")
        print(f"Input:    {input_path}")
        print(f"Backend:  {backend}")
        print(f"Optimize: O{optimize}")
        print()

    # Phase 1: Lexing
    t0 = time.time()
    tokens = tokenize(source, input_path)
    t1 = time.time()

    if verbose:
        print(f"[1/4] Lexer:    {len(tokens)} tokens ({(t1-t0)*1000:.1f}ms)")

    # Phase 2: Parsing
    ast = parse(tokens)
    t2 = time.time()

    if verbose:
        print(f"[2/4] Parser:   AST generated ({(t2-t1)*1000:.1f}ms)")

    # Phase 3: Code Generation
    if backend == 'llvm':
        ir = compile_to_llvm(source, input_path)
        t3 = time.time()

        if verbose:
            print(f"[3/4] CodeGen:  LLVM IR generated ({(t3-t2)*1000:.1f}ms)")

        # Determine output path
        if not output_path:
            output_path = os.path.splitext(input_path)[0] + '.ll'

        with open(output_path, 'w', encoding='utf-8') as f:
            f.write(ir)

        if verbose:
            ir_lines = ir.count('\n')
            print(f"[4/4] Output:   {output_path} ({ir_lines} lines)")

        print(f"Compiled: {input_path} -> {output_path}")

        # Run if requested
        if run_after:
            return cmd_run_ll(output_path, verbose)
        return 0

    elif backend == 'ast':
        if verbose:
            print(f"[3/4] AST:")
        print(print_ast(ast))
        return 0

    elif backend == 'tokens':
        if verbose:
            print(f"[3/4] Tokens:")
        lexer = MMCLexer(source, input_path)
        lexer.tokens = tokens
        print(lexer.format_tokens())
        return 0

    else:
        print(f"Error: Unknown backend '{backend}'")
        print("Supported backends: llvm, ast, tokens")
        return 1


def cmd_run_ll(ll_path: str, verbose: bool = False) -> int:
    """Compile LLVM IR to native binary and run."""
    base = os.path.splitext(ll_path)[0]
    s_path = base + '.s'
    bin_path = base

    if verbose:
        print()
        print("=== Running compiled program ===")

    # Step 1: llc - LLVM IR to assembly
    try:
        result = subprocess.run(
            ['llc', ll_path, '-o', s_path],
            capture_output=True, text=True, timeout=30
        )
        if result.returncode != 0:
            print(f"Error: llc failed:\n{result.stderr}")
            return 1
        if verbose:
            print(f"[llc]  {ll_path} -> {s_path}")
    except FileNotFoundError:
        print("Error: 'llc' not found. Install LLVM: pkg install llvm")
        return 1
    except subprocess.TimeoutExpired:
        print("Error: llc timed out")
        return 1

    # Step 2: gcc/clang - assembly to binary
    cc = 'gcc'
    try:
        result = subprocess.run(
            [cc, s_path, '-o', bin_path, '-lm'],
            capture_output=True, text=True, timeout=30
        )
        if result.returncode != 0:
            print(f"Error: {cc} failed:\n{result.stderr}")
            return 1
        if verbose:
            print(f"[gcc]  {s_path} -> {bin_path}")
    except FileNotFoundError:
        print("Error: 'gcc' not found. Install: pkg install clang")
        return 1

    # Step 3: Run the binary
    if verbose:
        print(f"[run]  ./{bin_path}")
        print("-" * 40)

    try:
        result = subprocess.run(
            [os.path.abspath(bin_path)],
            capture_output=True, text=True, timeout=10
        )
        if result.stdout:
            print(result.stdout, end='')
        if result.stderr:
            print(result.stderr, end='', file=sys.stderr)

        if verbose:
            print("-" * 40)
            print(f"Exit code: {result.returncode}")

        return result.returncode
    except subprocess.TimeoutExpired:
        print("Error: Program timed out (10s limit)")
        return 1


def cmd_run(args):
    """Shortcut: compile and run in one step."""
    args.run = True
    if not args.output:
        args.output = None
    return cmd_compile(args)


def cmd_version(args):
    """Show version information."""
    print(f"MMC Compiler v{MMC_VERSION}")
    print(f"  Myanmar Programming Language Transpiler")
    print(f"  Version: {MMC_VERSION} ({MMC_VERSION_MAJOR}.{MMC_VERSION_MINOR}.{MMC_VERSION_PATCH})")
    print(f"  Backends: LLVM IR, AST, Tokens")
    print(f"  Target:   aarch64-linux-android (Termux)")
    print()
    print(f"  License:  MIT")
    print(f"  Author:   MMC Compiler Team / Nyanlin-AI")
    return 0


def main():
    parser = argparse.ArgumentParser(
        prog='mmc',
        description=f'MMC Compiler v{MMC_VERSION} - Myanmar Programming Language'
    )
    subparsers = parser.add_subparsers(dest='command', help='Available commands')

    # compile command
    compile_parser = subparsers.add_parser('compile', help='Compile MMC source')
    compile_parser.add_argument('input', help='Input MMC file')
    compile_parser.add_argument('-o', '--output', help='Output file path')
    compile_parser.add_argument('-b', '--backend', choices=['llvm', 'ast', 'tokens'],
                               default='llvm', help='Code generation backend')
    compile_parser.add_argument('-r', '--run', action='store_true',
                               help='Compile and run immediately')
    compile_parser.add_argument('-v', '--verbose', action='store_true',
                               help='Verbose output')
    compile_parser.add_argument('-O', '--opt', type=int, default=0,
                               help='Optimization level (0-3)')

    # run command (shortcut)
    run_parser = subparsers.add_parser('run', help='Compile and run MMC source')
    run_parser.add_argument('input', help='Input MMC file')
    run_parser.add_argument('-o', '--output', help='Output file path')
    run_parser.add_argument('-v', '--verbose', action='store_true',
                           help='Verbose output')

    # tokenize command
    tok_parser = subparsers.add_parser('tokenize', help='Show tokens')
    tok_parser.add_argument('input', help='Input MMC file')

    # ast command
    ast_parser = subparsers.add_parser('ast', help='Show AST')
    ast_parser.add_argument('input', help='Input MMC file')

    # version command
    subparsers.add_parser('version', help='Show version info')

    args = parser.parse_args()

    if args.command == 'compile':
        return cmd_compile(args)
    elif args.command == 'run':
        return cmd_run(args)
    elif args.command == 'tokenize':
        args.backend = 'tokens'
        args.run = False
        args.verbose = True
        return cmd_compile(args)
    elif args.command == 'ast':
        args.backend = 'ast'
        args.run = False
        args.verbose = True
        return cmd_compile(args)
    elif args.command == 'version':
        return cmd_version(args)
    else:
        parser.print_help()
        return 0


if __name__ == "__main__":
    sys.exit(main())
