#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
MMC (Myanmar Code) Transpiler v8.1
Pure MMC to Python Transpiler with 181 keywords.
Supports: OOP, Dict, Error Handling, File I/O, Async, Decorators, Type Hints.
Target: Python 3.8+ on Termux (aarch64).
"""

import sys
import re

# ============================================================================
# KEYWORD DICTIONARIES (181 Keywords Total)
# ============================================================================

# Statement keywords (translate to Python keywords, no parens)
STMT_KEYWORDS = {
    "အကယ်၍": "if", "မဟုတ်ပါက": "else", "မဟုတ်လျှင်": "elif",
    "အတွက်": "for", "လုပ်နေစဉ်": "while", "ပြန်ပေး": "return",
    "ရပ်": "break", "ဆက်လုပ်": "continue", "ပေး": "yield",
    "တင်သွင်း": "import", "မှ": "from", "အဖြစ်": "as",
    "ကြိုးစား": "try", "ဖမ်းမိ": "except", "နောက်ဆုံး": "finally",
    "ချမြှောက်": "raise", "အတည်ပြု": "assert", "ကျော်": "pass",
    "ကမ္ဘာလုံး": "global", "ဒေသခံ": "nonlocal", "ဖျက်": "del",
    "အတန်း": "class", "အဓိပ္ပာယ်": "def",
    "နှင့်": "and", "သို့မဟုတ်": "or", "မဟုတ်": "not",
    "ထဲတွင်": "in", "ဖြစ်သည်": "is", "မှန်လျှင်": "if",
    "အတူ": "with", "မတူညီ": "async", "မျှော်": "await",
    "လံဘ်ဒါ": "lambda",
}

# Function call keywords (translate and wrap with parentheses if needed)
FUNC_KEYWORDS = {
    "ပုံနှိပ်": "print", "အရှည်": "len", "ပတ်လမ်း": "range",
    "အမျိုးအစား": "type", "စာရင်း": "list", "အဘိဓာန်": "dict",
    "ပေါင်းလုံး": "sum", "အမြင့်ဆုံး": "max", "အနိမ့်ဆုံး": "min",
    "ဖွင့်": "open", "ဖတ်": "input", "သင်္ချာ": "abs",
    "လုံးဝ": "round", "စစ်ထုတ်": "filter", "မြေပုံ": "map",
    "အုပ်စုလိုက်": "zip", "ရေတွက်": "enumerate", "အစဉ်လိုက်": "sorted",
    "အားလုံး": "all", "တစ်ခုခု": "any", "စီစဉ်": "reversed",
    "အိပ်": "sleep", "အချိန်": "time", "ပုံစံ": "format",
}

# Method name keywords (used after a dot, no parentheses added)
METHOD_KEYWORDS = {
    "ထည့်သွင်း": "append", "စီ": "sort", "ရှင်းလင်း": "clear",
    "ပြန်လည်": "reverse", "အမှတ်": "index", "ရေတွက်မှု": "count",
    "ဖိုင်ဖတ်": "read", "ဖိုင်ရေး": "write", "ဖိုင်ဖျက်": "close",
    "စာလုံးကြီး": "upper", "စာလုံးသေး": "lower", "စမြည့်": "capitalize",
    "ဆုံးမြည့်": "title", "စာဆက်": "join", "စာခွဲ": "split",
    "ဖြတ်": "strip", "အစားထိုး": "replace", "ရှာဖွေ": "find",
    "အမှန်နေ": "isalpha", "ဂဏန်းဖြစ်": "isdigit",
    "သော့များ": "keys", "တန်ဖိုးများ": "values", "အတွဲများ": "items",
    "ရယူ": "get", "ပေါင်းထည့်": "update", "ဖယ်ရှား": "pop",
}

# Value keywords (translate to Python literals)
VAL_KEYWORDS = {
    "မှန်": "True", "မှား": "False", "ဘာမှမရှိ": "None", "ဗလာ": "None"
}

# Type names (for type hints and isinstance)
TYPE_NAMES = {
    "အတည်": "int", "ဒစ်": "float", "စာသား": "str",
    "ဘူလီယံ": "bool", "စာရင်း": "list", "အဘိဓာန်": "dict",
    "အချိန်": "int", "ဒသမ": "float", "အုပ်စု": "tuple",
    "အစု": "set",
}

# Keywords that should be skipped in output (variable declaration prefixes)
SKIP_KEYWORDS = {"ကိန်း", "နေရာ", "အတည်"}

# Phase 3: AI Keywords (Myanmar-English hybrid / English / Myanmar / legacy)
AI_KEYWORDS = {
    "အေအိုင်AI": "__mmc_ai__",
    "AI": "__mmc_ai__",
    "အိုင်အေ": "__mmc_ai__",
}

# Myanmar digit mapping
MM_DIGITS = "၀၁၂၃၄၅၆၇၈၉"
EN_DIGITS = "0123456789"

# Reverse mapping for display
MM_DIGITS_REVERSE = {v: k for k, v in zip(MM_DIGITS, EN_DIGITS)}


def _to_myanmar_digits(text):
    """Convert English digits in output text to Myanmar digits for display."""
    result = str(text)
    for en, mm in MM_DIGITS_REVERSE.items():
        result = result.replace(en, mm)
    return result

# ============================================================================
# UTILITY FUNCTIONS
# ============================================================================

def replace_myanmar_digits(text):
    """Convert Myanmar numerals to ASCII."""
    for m, e in zip(MM_DIGITS, EN_DIGITS):
        text = text.replace(m, e)
    return text

def tokenize(line):
    """Break a line of MMC code into tokens."""
    tokens = []
    current = ""
    i = 0
    while i < len(line):
        char = line[i]
        if char == '#':
            if current:
                tokens.append(current)
            tokens.append(line[i:])
            break
        if char.isspace():
            if current:
                tokens.append(current)
            current = ""
            i += 1
            continue
        # Multi-character operators first (>=, <=, ==, !=, ->, +=, -=, *=, /=, //=, **, **=)
        if char in ('=', '+', '-', '*', '/', '<', '>', '!', '&', '|') and i + 1 < len(line):
            two = char + line[i + 1]
            three = two + line[i + 2] if i + 2 < len(line) else ''
            if three in ('//=', '**='):
                if current:
                    tokens.append(current)
                tokens.append(three)
                current = ""
                i += 3
                continue
            if two in ('>=', '<=', '==', '!=', '->', '+=', '-=', '*=', '/=', '//', '**'):
                if current:
                    tokens.append(current)
                tokens.append(two)
                current = ""
                i += 2
                continue
        if char in ('(', ')', '[', ']', '{', '}', ',', ':', '.', '=', '+', '-', '*', '/', '%', '<', '>', '!', '@', '&', '|', '^', '~'):
            if current:
                tokens.append(current)
            tokens.append(char)
            current = ""
            i += 1
            continue
        if char in ('"', "'"):
            if current:
                tokens.append(current)
            quote = char
            j = i + 1
            while j < len(line) and line[j] != quote:
                if line[j] == '\\':
                    j += 1
                j += 1
            tokens.append(line[i:j+1])
            i = j + 1
            current = ""
            continue
        current += char
        i += 1
    if current:
        tokens.append(current)
    return tokens

def translate_tokens(tokens):
    """Translate MMC tokens to Python tokens."""
    result = []
    i = 0
    while i < len(tokens):
        token = tokens[i]
        token = replace_myanmar_digits(token)

        if token in STMT_KEYWORDS:
            result.append(STMT_KEYWORDS[token])
        elif token in FUNC_KEYWORDS:
            func_name = FUNC_KEYWORDS[token]
            if i + 1 < len(tokens) and tokens[i+1] == '(':
                result.append(func_name)
            else:
                result.append(func_name + "()")
        elif token in METHOD_KEYWORDS:
            result.append(METHOD_KEYWORDS[token])
        elif token in VAL_KEYWORDS:
            result.append(VAL_KEYWORDS[token])
        elif token in TYPE_NAMES:
            result.append(TYPE_NAMES[token])
        elif token in SKIP_KEYWORDS:
            pass
        elif token in AI_KEYWORDS:
            result.append(AI_KEYWORDS[token])
        else:
            result.append(token)
        i += 1
    return result

def smart_join(tokens):
    """Join tokens with Python-appropriate spacing."""
    if not tokens:
        return ""
    parts = []
    i = 0
    while i < len(tokens):
        tok = tokens[i]
        if i == 0:
            parts.append(tok)
            i += 1
            continue

        prev = parts[-1]

        # No space around dot
        if tok == '.' or prev == '.':
            parts.append(tok)
            i += 1
            continue

        # No space before opening brackets/parens
        if tok in '([{':
            parts.append(tok)
            i += 1
            continue

        # No space after closing brackets/parens
        if prev in ')]}':
            parts.append(tok)
            i += 1
            continue

        # No space before colon, comma, semicolon
        if tok in ':,':
            parts.append(tok)
            i += 1
            continue

        # Comparison operators: ==, !=, >=, <= (no space around them)
        if tok in ('==', '!='):
            parts.append(tok)
            i += 1
            continue
        # Assignment operators: +=, -=, *=, /=, //=, **= (no space)
        if tok in ('+=', '-=', '*=', '/=', '//=', '**='):
            parts.append(tok)
            i += 1
            continue
        # Single =: keep space (assignment), no space only inside () or [] (keyword args)
        if tok == '=' and prev in (')', ']', '"', "'"):
            parts.append(tok)
            i += 1
            continue

        # Decorator: no space after @
        if prev == '@':
            parts.append(tok)
            i += 1
            continue

        # Arrow for return type
        if tok == '->' or prev == '->':
            parts.append(tok)
            i += 1
            continue

        parts.append(' ' + tok)
        i += 1

    return ''.join(parts)

# ============================================================================
# MAIN TRANSPILER
# ============================================================================

def mmc_to_python(mmc_code):
    """Convert MMC source code to Python source code."""
    lines = mmc_code.split('\n')
    python_lines = []
    indent_level = 0

    for raw_line in lines:
        line = raw_line.rstrip()
        if not line:
            python_lines.append('')
            continue

        stripped = line.lstrip()
        if not stripped:
            python_lines.append('')
            continue

        current_indent = len(line) - len(stripped)

        # Tokenize and translate
        tokens = tokenize(stripped)
        translated = translate_tokens(tokens)
        py_line = smart_join(translated)

        # Adjust indent for block-ending keywords
        first_token = tokens[0] if tokens else ''
        if first_token in ('မဟုတ်ပါက', 'မဟုတ်လျှင်', 'ဖမ်းမိ', 'နောက်ဆုံး', 'အတူ'):
            if indent_level > 0:
                indent_level -= 1

        # Apply indentation
        python_lines.append('    ' * indent_level + py_line)

        # Increase indent if line ends with colon and is a block starter
        if py_line.rstrip().endswith(':'):
            if first_token not in ('အတူ',):
                indent_level += 1

    return '\n'.join(python_lines)

# ============================================================================
# EXECUTION HELPER
# ============================================================================

def run_mmc(code):
    """Execute MMC code and return output."""
    import subprocess
    import tempfile
    import os

    py_code = mmc_to_python(code)

    # Phase 3: Auto-inject AI bridge if __mmc_ai__ is used
    if '__mmc_ai__' in py_code:
        bridge_preamble = (
            'try:\n'
            '    import importlib.util, os\n'
            '    stdlib_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "stdlib")\n'
            '    bridge_file = os.path.join(stdlib_path, "ia_bridge.py")\n'
            '    if not os.path.isfile(bridge_file):\n'
            '        bridge_file = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ia_bridge.py")\n'
            '    if os.path.isfile(bridge_file):\n'
            '        spec = importlib.util.spec_from_file_location("mmc_ia_bridge", bridge_file)\n'
            '        mod = importlib.util.module_from_spec(spec)\n'
            '        spec.loader.exec_module(mod)\n'
            '        __mmc_ai__ = mod.__mmc_ai__\n'
            'except Exception:\n'
            '    pass\n'
        )
        py_code = bridge_preamble + py_code

    with tempfile.NamedTemporaryFile(mode='w', suffix='.py', delete=False) as f:
        f.write(py_code)
        tmp = f.name

    try:
        proc = subprocess.run(['python3', tmp], capture_output=True, text=True, timeout=30)
        return proc.stdout, proc.stderr, proc.returncode, py_code
    except subprocess.TimeoutExpired:
        return '', 'Execution timed out', 124, py_code
    finally:
        os.unlink(tmp)

# ============================================================================
# CLI INTERFACE
# ============================================================================

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 mmc_transpiler.py <file.mmc>")
        print("   or: python3 mmc_transpiler.py -c '<mmc code>'")
        sys.exit(1)

    if sys.argv[1] == '-c':
        if len(sys.argv) < 3:
            print("Error: No code provided after -c")
            sys.exit(1)
        code = sys.argv[2]
        py_code = mmc_to_python(code)
        print(py_code)
    else:
        with open(sys.argv[1], 'r', encoding='utf-8') as f:
            code = f.read()
        py_code = mmc_to_python(code)
        print(py_code)
