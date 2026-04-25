#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# =================================================================
# MMC Native Pipeline - Bootstrap Compiler v1.0
# Phase 3.6: Self-Hosting Transition
#
# Executes the complete MMC-Native Compiler pipeline:
#   MMC Source -> mmc_lexer.mmc -> mmc_parser.mmc -> mmc_codegen.mmc -> Python
#
# Usage:
#   python3 compile_mmc.py <file.mmc>           # Compile MMC to Python
#   python3 compile_mmc.py <file.mmc> --run     # Compile and execute
#   python3 compile_mmc.py <file.mmc> --ast     # Show AST
#   python3 compile_mmc.py <file.mmc> --tokens  # Show tokens
#   python3 compile_mmc.py -c '<mmc code>'      # Compile inline MMC code
#
# Pipeline Architecture:
#   [MMC Source File]
#       |
#       v
#   mmc_lexer.mmc  (MMCLexer.tokenize)
#       |  -> Token[]
#       v
#   mmc_parser.mmc (MMCParser.parse)
#       |  -> AST (dict with 'type' fields)
#       v
#   mmc_codegen.mmc (MMCCodeGenerator.generate_program)
#       |  -> Python 3 Source Code String
#       v
#   [Python Output / Execution]
#
# IA Native Mapping (data contract):
#   Lexer: 'အေအိုင်AI'|'AI'|'ia'|'အိုင်အေ' -> Token(type=IA)
#   Parser: IA + '.' + method -> AST{"type":"IACall","method":"say","args":[...]}
#   CodeGen: IACall -> "__mmc_ai__.say(...)"
#
# GitHub: meonnmi-ops/mmc-compiler
# =================================================================

import sys
import os
import json
import importlib.util
import tempfile
import subprocess

# =================================================================
# Configuration
# =================================================================

SELFHOSTED_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "selfhosted")


def load_mmc_module(name, filepath):
    """Dynamically load an MMC module (.mmc file) as a Python module.

    The MMC files are written in Python syntax and can be loaded directly.
    This is the bootstrap mechanism — eventually MMC will compile itself.
    Uses exec() approach since .mmc extension isn't recognized by importlib.
    """
    with open(filepath, 'r', encoding='utf-8') as f:
        source_code = f.read()

    module = type(name, (), {})()
    sys.modules[name] = module
    exec(compile(source_code, filepath, 'exec'), module.__dict__)
    return module


# =================================================================
# Pipeline Stages
# =================================================================

def stage_lex(source, source_name="<mmc>"):
    """Stage 1: Lexing — MMC Source -> Token Stream.

    Uses mmc_lexer.mmc (MMCLexer) to tokenize MMC source code.

    Data Contract Output:
        List of dicts: {"type": str, "value": str, "line": int, "col": int}
        Token types: WORD, NUMBER, STRING, FSTRING, OP, NEWLINE,
                     COMMENT, INDENT, DEDENT, EOF, IA, UNKNOWN

    IA Handling:
        All 4 variants ('အေအိုင်AI', 'AI', 'ia', 'အိုင်အေ')
        produce Token(type="IA", value="<variant>")
    """
    lexer_mod = load_mmc_module("mmc_lexer", os.path.join(SELFHOSTED_DIR, "mmc_lexer.mmc"))
    lexer = lexer_mod.MMCLexer(source)
    tokens = lexer.tokenize(source)

    if lexer.has_errors():
        print("=== Lexer Errors ===", file=sys.stderr)
        for err in lexer.get_errors():
            print("  " + err, file=sys.stderr)
        print("", file=sys.stderr)

    return tokens, lexer


def stage_parse(tokens, source_name="<mmc>"):
    """Stage 2: Parsing — Token Stream -> AST.

    Uses mmc_parser.mmc (MMCParser) to build an Abstract Syntax Tree.

    Data Contract Output:
        AST dict: {"type": "Program", "body": [statement_nodes...]}
        Each node: {"type": str, ...node-specific fields...}

        Key node types matching mmc_codegen.mmc:
        - Statement: Assign, If, For, While, FunctionDef, ClassDef, ...
        - Expression: BinOp, Call, IACall, Name, Constant, ...
        - Operators: Add, Sub, Mult, Div, And, Or, Not, Eq, Lt, ...

    IA Handling:
        Token(IA) + '.' + method -> {"type": "IACall", "method": "say", "args": [...]}
    """
    # Convert MMCToken objects to dicts for parser
    token_dicts = [t.to_dict() for t in tokens]

    parser_mod = load_mmc_module("mmc_parser", os.path.join(SELFHOSTED_DIR, "mmc_parser.mmc"))
    parser = parser_mod.MMCParser(token_dicts)
    ast = parser.parse(token_dicts)

    if parser.has_errors():
        print("=== Parser Errors ===", file=sys.stderr)
        for err in parser.get_errors():
            print("  " + err, file=sys.stderr)
        print("", file=sys.stderr)

    return ast, parser


def stage_codegen(ast, source_name="<mmc>"):
    """Stage 3: Code Generation — AST -> Python Source Code.

    Uses mmc_codegen.mmc (MMCCodeGenerator) to generate Python code.

    Data Contract Input:
        AST dict from mmc_parser.mmc with "type" fields matching
        mmc_codegen.mmc NODE_* constants.

    Data Contract Output:
        String of valid Python 3 source code.

    IA Handling:
        {"type": "IACall", "method": "say", "args": [...]}
        -> "__mmc_ai__.say(...)"
    """
    codegen_mod = load_mmc_module("mmc_codegen", os.path.join(SELFHOSTED_DIR, "mmc_codegen.mmc"))
    codegen = codegen_mod.MMCCodeGenerator()
    python_code = codegen.generate_program(ast.get("body", []))

    if codegen.get_errors():
        print("=== CodeGen Errors ===", file=sys.stderr)
        for err in codegen.get_errors():
            print("  " + err, file=sys.stderr)
        print("", file=sys.stderr)

    return python_code, codegen


# =================================================================
# Full Pipeline
# =================================================================

def compile_mmc(source, source_name="<mmc>", show_tokens=False, show_ast=False):
    """Run the complete MMC-Native Compiler pipeline.

    Pipeline:
        MMC Source -> Lexer -> Tokens -> Parser -> AST -> CodeGen -> Python Code

    Args:
        source: MMC source code string.
        source_name: Name for error messages.
        show_tokens: If True, print token stream.
        show_ast: If True, print AST.

    Returns:
        Tuple of (python_code, tokens, ast, lexer, parser, codegen)
    """
    print("=" * 60, file=sys.stderr)
    print("MMC Native Pipeline v1.0", file=sys.stderr)
    print("=" * 60, file=sys.stderr)

    # Stage 1: Lex
    print("[Stage 1/3] Lexing...", file=sys.stderr)
    tokens, lexer = stage_lex(source, source_name)
    non_trivial = [t for t in tokens if t.type not in ("NEWLINE", "INDENT", "DEDENT", "EOF")]
    print("  -> %d tokens (%d non-trivial)" % (len(tokens), len(non_trivial)), file=sys.stderr)

    if show_tokens:
        print("\n=== Token Stream ===")
        for t in tokens:
            if t.type not in ("NEWLINE", "INDENT", "DEDENT"):
                print("  %s" % repr(t))

    # Stage 2: Parse
    print("[Stage 2/3] Parsing...", file=sys.stderr)
    ast, parser = stage_parse(tokens, source_name)
    stmt_count = len(ast.get("body", []))
    print("  -> %d top-level statements" % stmt_count, file=sys.stderr)

    if show_ast:
        print("\n=== AST ===")
        print(json.dumps(ast, indent=2, ensure_ascii=False, default=str))

    # Stage 3: CodeGen
    print("[Stage 3/3] Code Generation...", file=sys.stderr)
    python_code, codegen = stage_codegen(ast, source_name)

    # Inject __mmc_ai__ stub if IA calls are present
    if "__mmc_ai__" in python_code:
        ia_stub = (
            "# Auto-generated IA bridge stub (Phase 3.6)\n"
            "class _MMC_AI_Bridge:\n"
            "    def say(self, *args, **kwargs):\n"
            "        print(*args, **kwargs)\n"
            "    def think(self, *args, **kwargs):\n"
            "        print('[think]', *args, **kwargs)\n"
            "    def explain(self, *args, **kwargs):\n"
            "        print('[explain]', *args, **kwargs)\n"
            "    def learn(self, *args, **kwargs):\n"
            "        print('[learn]', *args, **kwargs)\n"
            "    def connect(self, *args, **kwargs):\n"
            "        print('[connect]', *args, **kwargs)\n"
            "    def update(self, *args, **kwargs):\n"
            "        print('[update]', *args, **kwargs)\n"
            "    def check(self, *args, **kwargs):\n"
            "        print('[check]', *args, **kwargs)\n"
            "    def ask(self, *args, **kwargs):\n"
            "        print('[ask]', *args, **kwargs)\n"
            "    def describe(self, *args, **kwargs):\n"
            "        print('[describe]', *args, **kwargs)\n"
            "    def teach(self, *args, **kwargs):\n"
            "        print('[teach]', *args, **kwargs)\n"
            "    def visualize(self, *args, **kwargs):\n"
            "        print('[visualize]', *args, **kwargs)\n"
            "    def generate(self, *args, **kwargs):\n"
            "        print('[generate]', *args, **kwargs)\n"
            "    def respond(self, *args, **kwargs):\n"
            "        print('[respond]', *args, **kwargs)\n"
            "    def analyze(self, *args, **kwargs):\n"
            "        print('[analyze]', *args, **kwargs)\n"
            "    def chat(self, *args, **kwargs):\n"
            "        print('[chat]', *args, **kwargs)\n"
            "    def translate(self, *args, **kwargs):\n"
            "        print('[translate]', *args, **kwargs)\n"
            "    def summarize(self, *args, **kwargs):\n"
            "        print('[summarize]', *args, **kwargs)\n"
            "    def dream(self, *args, **kwargs):\n"
            "        print('[dream]', *args, **kwargs)\n"
            "__mmc_ai__ = _MMC_AI_Bridge()\n"
            "\n"
        )
        python_code = ia_stub + python_code
        print("  -> Injected __mmc_ai__ bridge stub", file=sys.stderr)

    py_lines = [l for l in python_code.split("\n") if l.strip()]
    print("  -> %d lines of Python code" % len(py_lines), file=sys.stderr)

    print("=" * 60, file=sys.stderr)
    print("Pipeline complete.", file=sys.stderr)

    return python_code, tokens, ast, lexer, parser, codegen


def run_python(python_code, source_name="<mmc>", timeout=30):
    """Execute generated Python code and return output."""
    with tempfile.NamedTemporaryFile(mode='w', suffix='.py', delete=False, encoding='utf-8') as f:
        f.write(python_code)
        tmp_path = f.name

    try:
        proc = subprocess.run(
            ['python3', tmp_path],
            capture_output=True, text=True, timeout=timeout
        )
        return proc.stdout, proc.stderr, proc.returncode
    except subprocess.TimeoutExpired:
        return '', 'Execution timed out after %d seconds' % timeout, 124
    finally:
        os.unlink(tmp_path)


# =================================================================
# CLI Interface
# =================================================================

def main():
    """Main CLI entry point."""
    args = sys.argv[1:]

    if not args:
        print("MMC Native Pipeline v1.0")
        print("Usage: python3 compile_mmc.py <file.mmc> [options]")
        print("")
        print("Options:")
        print("  --run       Compile and execute the output")
        print("  --ast       Show the parsed AST")
        print("  --tokens    Show the token stream")
        print("  -c <code>   Compile inline MMC code")
        print("  --compare   Compare output with mmc_transpiler.py")
        sys.exit(1)

    # Parse arguments
    show_ast = "--ast" in args
    show_tokens = "--tokens" in args
    do_run = "--run" in args
    do_compare = "--compare" in args
    inline_mode = "-c" in args

    # Filter out flags
    clean_args = [a for a in args if not a.startswith("--") and a != "-c"]

    if inline_mode and len(clean_args) >= 1:
        source = clean_args[0]
        source_name = "<inline>"
    elif len(clean_args) >= 1:
        filepath = clean_args[0]
        if not os.path.exists(filepath):
            print("Error: File not found: %s" % filepath, file=sys.stderr)
            sys.exit(1)
        with open(filepath, 'r', encoding='utf-8') as f:
            source = f.read()
        source_name = filepath
    else:
        print("Error: No source provided", file=sys.stderr)
        sys.exit(1)

    # Run the pipeline
    python_code, tokens, ast, lexer, parser, codegen = compile_mmc(
        source, source_name, show_tokens=show_tokens, show_ast=show_ast
    )

    # Output generated Python code
    print("\n" + "=" * 60)
    print("Generated Python Code:")
    print("=" * 60)
    print(python_code)

    # Compare with mmc_transpiler.py if requested
    if do_compare:
        transpiler_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "mmc_transpiler.py")
        if os.path.exists(transpiler_path):
            sys.path.insert(0, os.path.dirname(transpiler_path))
            import importlib
            if os.path.basename(transpiler_path)[:-3] in sys.modules:
                del sys.modules[os.path.basename(transpiler_path)[:-3]]
            spec = importlib.util.spec_from_file_location("mmc_transpiler", transpiler_path)
            transpiler_mod = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(transpiler_mod)

            transpiled = transpiler_mod.mmc_to_python(source)

            print("\n" + "=" * 60)
            print("Transpiler Output (mmc_transpiler.py):")
            print("=" * 60)
            print(transpiled)

            if python_code.strip() == transpiled.strip():
                print("\n*** OUTPUT MATCHES mmc_transpiler.py ***")
            else:
                print("\n*** OUTPUT DIFFERS from mmc_transpiler.py ***")
                print("(This is expected — the AST-based pipeline may differ in formatting)")

    # Execute if requested
    if do_run:
        print("\n" + "=" * 60)
        print("Execution Output:")
        print("=" * 60)
        stdout, stderr, returncode = run_python(python_code, source_name)
        if stdout:
            print(stdout, end='')
        if stderr:
            print(stderr, end='', file=sys.stderr)
        print("\nExit code: %d" % returncode)


if __name__ == "__main__":
    main()
