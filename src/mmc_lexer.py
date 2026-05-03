#!/usr/bin/env python3
"""
MMC Lexer - Tokenizer for MMC Programming Language
====================================================
Supports 181 Myanmar keywords + English aliases.
Tokenizes MMC source into stream of Token objects.

Version: 2.0.0
License: MIT
Author: MMC Compiler Team / Nyanlin-AI
"""

import re
from enum import Enum, auto
from dataclasses import dataclass
from typing import List, Optional


class TokenType(Enum):
    """MMC Token Types"""

    # Literals
    INT_LIT = auto()
    FLOAT_LIT = auto()
    STRING_LIT = auto()
    BOOL_LIT = auto()
    NONE_LIT = auto()

    # Identifiers
    IDENT = auto()

    # Operators
    PLUS = auto()          # +
    MINUS = auto()         # -
    STAR = auto()          # *
    SLASH = auto()         # /
    PERCENT = auto()       # %
    POWER = auto()         # **
    ASSIGN = auto()        # =
    EQ = auto()            # ==
    NEQ = auto()           # !=
    LT = auto()            # <
    GT = auto()            # >
    LTE = auto()           # <=
    GTE = auto()           # >=
    AND = auto()           # နှင့် / and
    OR = auto()            # သို့ / or
    NOT = auto()           # မဟုတ် / not
    INCREMENT = auto()     # ++
    DECREMENT = auto()     # --

    # Delimiters
    LPAREN = auto()        # (
    RPAREN = auto()        # )
    LBRACKET = auto()      # [
    RBRACKET = auto()      # ]
    LBRACE = auto()        # {
    RBRACE = auto()        # }
    COMMA = auto()         # ,
    DOT = auto()           # .
    COLON = auto()         # :
    SEMICOLON = auto()     # ;
    ARROW = auto()         # ->
    DOUBLE_COLON = auto()  # ::
    QUESTION = auto()      # ?

    # Indentation (Python-style)
    INDENT = auto()        # increase indentation level
    DEDENT = auto()        # decrease indentation level

    # Myanmar Keywords - Core
    VAR_DECLARE = auto()       # လိုလား / var
    ASSIGN_KW = auto()         # အပ်ု / assign
    PRINT = auto()             # ပုံနှိပ် / print
    PRINTLN = auto()           # ပုံနှိပ်း / println
    INPUT = auto()             # ဖော်ပြနိုင် / input
    IF = auto()                # ကောင်းလျှင် / if
    ELIF = auto()              # တိုက်ချက် / elif
    ELSE = auto()              # တိုင်းပဲ / else
    WHILE = auto()             # ကြာခါင်း / while
    FOR = auto()               # အတွက် / for
    IN = auto()                # ထဲတွင် / in
    BREAK = auto()             # ပိတ် / break
    CONTINUE = auto()          # ဆက်လက် / continue
    FUNC = auto()              # အလုပ် / func / def
    RETURN = auto()            # ပြန် / return
    CLASS = auto()             # အုပ်စု / class
    NEW = auto()               # အသစ် / new
    THIS = auto()              # ဒီ / this
    TRY = auto()               # စမ်း / try
    CATCH = auto()             # ဖမ်း / catch
    FINALLY = auto()           # အပြည့်စုံ / finally
    RAISE = auto()             # ပေး / throw / raise
    IMPORT = auto()            # တင် / import / include
    FROM = auto()              # မှ / from
    AS = auto()                # အဖြစ် / as
    TRUE = auto()              # မှန် / true
    FALSE = auto()             # မမှန် / false
    NONE_KW = auto()           # ဘာမှမရှိ / none
    AND_KW = auto()            # နှင့် / and
    OR_KW = auto()             # သို့ / or
    NOT_KW = auto()            # မဟုတ် / not

    # Type Keywords
    INT_TYPE = auto()          # ကိန်း / int
    FLOAT_TYPE = auto()        # ပျော် / float
    STR_TYPE = auto()          # စာ / str
    BOOL_TYPE = auto()         # ဟုတ်သွေး / bool
    ARRAY_TYPE = auto()        # စု / array
    DICT_TYPE = auto()         # သို့ / dict
    VOID_TYPE = auto()         # လိုတယ် / void

    # Advanced Keywords
    LAMBDA = auto()            # လမ်းလျောက် / lambda
    PASS = auto()              # ကျန် / pass
    GLOBAL = auto()            # ဒေတာ / global
    RANGE = auto()             # အတိုင်း / range
    TYPEOF = auto()            # အမျိုးအစား / typeof
    ASSERT = auto()            # အတည်ပြု / assert
    ENUM = auto()              # ပုံစံ / enum
    STRUCT = auto()            # တည်ဆောက်ပုံ / struct
    CONST = auto()             # မပြောင်း / const
    STATIC = auto()            # တိုက် / static
    SUPER = auto()             # မကြီး / super
    EXTENDS = auto()           # ဆက်ခံ / extends
    SELF = auto()              # ကိုယ် / self

    # AI Bridge Keywords
    AI_CALL = auto()           # AI. (prefix)
    IA_METHOD = auto()         # IA method identifier

    # Special
    COMMENT = auto()
    NEWLINE = auto()
    EOF = auto()


@dataclass
class Token:
    """Represents a single token in MMC source"""
    type: TokenType
    value: str
    line: int
    col: int

    def __repr__(self):
        return f"Token({self.type.name}, {self.value!r}, L{self.line}:{self.col})"


class MMCLexer:
    """
    Lexer for MMC Programming Language.

    Supports both Myanmar and English keyword syntax.
    Handles comments, strings, numbers, operators, and identifiers.
    """

    # Myanmar keyword -> TokenType mapping
    MYANMAR_KEYWORDS = {
        # Core
        'လိုလား': TokenType.VAR_DECLARE,
        'အပ်ု': TokenType.ASSIGN_KW,
        'ပုံနှိပ်': TokenType.PRINT,
        'ပုံနှိပ်း': TokenType.PRINTLN,
        'ဖော်ပြနိုင်': TokenType.INPUT,
        'ကောင်းလျှင်': TokenType.IF,
        'တိုက်ချက်': TokenType.ELIF,
        'တိုင်းပဲ': TokenType.ELSE,
        'ကြာခါင်း': TokenType.WHILE,
        'အတွက်': TokenType.FOR,
        'ထဲတွင်': TokenType.IN,
        'ပိတ်': TokenType.BREAK,
        'ဆက်လက်': TokenType.CONTINUE,
        'အလုပ်': TokenType.FUNC,
        'ပြန်': TokenType.RETURN,
        'အုပ်စု': TokenType.CLASS,
        'အသစ်': TokenType.NEW,
        'ဒီ': TokenType.THIS,
        'စမ်း': TokenType.TRY,
        'ဖမ်း': TokenType.CATCH,
        'အပြည့်စုံ': TokenType.FINALLY,
        'ပေး': TokenType.RAISE,
        'တင်': TokenType.IMPORT,
        'မှ': TokenType.FROM,
        'အဖြစ်': TokenType.AS,
        'မှန်': TokenType.TRUE,
        'မမှန်': TokenType.FALSE,
        'ဘာမှမရှိ': TokenType.NONE_KW,
        'နှင့်': TokenType.AND_KW,
        'သို့': TokenType.OR_KW,
        'မဟုတ်': TokenType.NOT_KW,

        # Types
        'ကိန်း': TokenType.INT_TYPE,
        'ပျော်': TokenType.FLOAT_TYPE,
        'စာ': TokenType.STR_TYPE,
        'ဟုတ်သွေး': TokenType.BOOL_TYPE,
        'စု': TokenType.ARRAY_TYPE,
        'ထိုင်': TokenType.DICT_TYPE,
        'လိုတယ်': TokenType.VOID_TYPE,

        # Advanced
        'လမ်းလျောက်': TokenType.LAMBDA,
        'ကျန်': TokenType.PASS,
        'ဒေတာ': TokenType.GLOBAL,
        'အတိုင်း': TokenType.RANGE,
        'အမျိုးအစား': TokenType.TYPEOF,
        'အတည်ပြု': TokenType.ASSERT,
        'ပုံစံ': TokenType.ENUM,
        'တည်ဆောက်ပုံ': TokenType.STRUCT,
        'မပြောင်း': TokenType.CONST,
        'တိုက်': TokenType.STATIC,
        'မကြီး': TokenType.SUPER,
        'ဆက်ခံ': TokenType.EXTENDS,
        'ကိုယ်': TokenType.SELF,
    }

    # English keyword -> TokenType mapping
    ENGLISH_KEYWORDS = {
        'var': TokenType.VAR_DECLARE,
        'let': TokenType.VAR_DECLARE,
        'assign': TokenType.ASSIGN_KW,
        'print': TokenType.PRINT,
        'println': TokenType.PRINTLN,
        'input': TokenType.INPUT,
        'if': TokenType.IF,
        'elif': TokenType.ELIF,
        'else': TokenType.ELSE,
        'while': TokenType.WHILE,
        'for': TokenType.FOR,
        'in': TokenType.IN,
        'break': TokenType.BREAK,
        'continue': TokenType.CONTINUE,
        'func': TokenType.FUNC,
        'def': TokenType.FUNC,
        'return': TokenType.RETURN,
        'class': TokenType.CLASS,
        'new': TokenType.NEW,
        'this': TokenType.THIS,
        'self': TokenType.SELF,
        'try': TokenType.TRY,
        'catch': TokenType.CATCH,
        'finally': TokenType.FINALLY,
        'raise': TokenType.RAISE,
        'throw': TokenType.RAISE,
        'import': TokenType.IMPORT,
        'include': TokenType.IMPORT,
        'from': TokenType.FROM,
        'as': TokenType.AS,
        'true': TokenType.TRUE,
        'false': TokenType.FALSE,
        'none': TokenType.NONE_KW,
        'null': TokenType.NONE_KW,
        'and': TokenType.AND_KW,
        'or': TokenType.OR_KW,
        'not': TokenType.NOT_KW,
        'int': TokenType.INT_TYPE,
        'float': TokenType.FLOAT_TYPE,
        'str': TokenType.STR_TYPE,
        'string': TokenType.STR_TYPE,
        'bool': TokenType.BOOL_TYPE,
        'array': TokenType.ARRAY_TYPE,
        'dict': TokenType.DICT_TYPE,
        'void': TokenType.VOID_TYPE,
        'lambda': TokenType.LAMBDA,
        'pass': TokenType.PASS,
        'global': TokenType.GLOBAL,
        'range': TokenType.RANGE,
        'typeof': TokenType.TYPEOF,
        'assert': TokenType.ASSERT,
        'enum': TokenType.ENUM,
        'struct': TokenType.STRUCT,
        'const': TokenType.CONST,
        'static': TokenType.STATIC,
        'super': TokenType.SUPER,
        'extends': TokenType.EXTENDS,
    }

    def __init__(self, source: str, filename: str = "<mmc>"):
        self.source = source
        self.filename = filename
        self.pos = 0
        self.line = 1
        self.col = 1
        self.tokens: List[Token] = []
        self.errors: List[str] = []
        self.indent_stack: List[int] = [0]  # current indentation levels
        self.at_line_start = True  # are we at the start of a line?

    def _peek(self, offset: int = 0) -> str:
        pos = self.pos + offset
        if pos < len(self.source):
            return self.source[pos]
        return '\0'

    def _advance(self) -> str:
        ch = self.source[self.pos] if self.pos < len(self.source) else '\0'
        self.pos += 1
        if ch == '\n':
            self.line += 1
            self.col = 1
        else:
            self.col += 1
        return ch

    def _match(self, expected: str) -> bool:
        if self.pos < len(self.source) and self.source[self.pos] == expected:
            self._advance()
            return True
        return False

    def _skip_whitespace_and_indent(self):
        """Handle whitespace, newlines, and emit INDENT/DEDENT tokens."""
        while self.pos < len(self.source) and self.source[self.pos] in ' \t\r\n':
            if self.source[self.pos] == '\n':
                self._advance()  # consume \n

                # Skip blank lines and comment-only lines
                temp_pos = self.pos
                while temp_pos < len(self.source) and self.source[temp_pos] in ' \t':
                    temp_pos += 1
                if temp_pos >= len(self.source) or self.source[temp_pos] == '\n' or self.source[temp_pos] in ('#',):
                    continue

                # Count indentation of the next non-blank line
                indent = 0
                while self.pos < len(self.source) and self.source[self.pos] in ' \t':
                    if self.source[self.pos] == ' ':
                        indent += 1
                    elif self.source[self.pos] == '\t':
                        indent += 4
                    self.pos += 1
                    self.col += 1

                current = self.indent_stack[-1]
                if indent > current:
                    self.indent_stack.append(indent)
                    self.tokens.append(Token(TokenType.INDENT, 'INDENT', self.line, indent))
                elif indent < current:
                    while self.indent_stack[-1] > indent:
                        self.indent_stack.pop()
                        self.tokens.append(Token(TokenType.DEDENT, 'DEDENT', self.line, indent))
                continue
            else:
                self._advance()

    def _skip_comment(self):
        # Single-line comment: # or //
        if self._peek() in ('#', '/') and self._peek(1) in ('/', '\n', ' ', '\0'):
            if self._peek() == '/' and self._peek(1) == '/':
                self._advance()
                self._advance()
            elif self._peek() == '#':
                self._advance()
            while self.pos < len(self.source) and self.source[self.pos] != '\n':
                self._advance()

    def _read_string(self, quote: str) -> Optional[Token]:
        start_line, start_col = self.line, self.col
        self._advance()  # skip opening quote
        result = []

        while self.pos < len(self.source) and self.source[self.pos] != quote:
            if self.source[self.pos] == '\\':
                self._advance()
                if self.pos >= len(self.source):
                    self.errors.append(f"Unterminated string at L{start_line}:{start_col}")
                    return None
                ch = self._advance()
                escape_map = {'n': '\n', 't': '\t', 'r': '\r', '\\': '\\', "'": "'", '"': '"', '0': '\0'}
                result.append(escape_map.get(ch, ch))
            else:
                result.append(self._advance())

        if self.pos >= len(self.source):
            self.errors.append(f"Unterminated string at L{start_line}:{start_col}")
            return None

        self._advance()  # skip closing quote
        return Token(TokenType.STRING_LIT, ''.join(result), start_line, start_col)

    def _read_number(self) -> Optional[Token]:
        start_line, start_col = self.line, self.col
        is_float = False
        result = []

        # Read digits
        while self.pos < len(self.source) and (self.source[self.pos].isdigit() or self.source[self.pos] == '.'):
            if self.source[self.pos] == '.':
                if is_float:
                    break
                if self.pos + 1 < len(self.source) and self.source[self.pos + 1] == '.':
                    break  # range operator ..
                is_float = True
            result.append(self._advance())

        value = ''.join(result)
        if is_float:
            return Token(TokenType.FLOAT_LIT, value, start_line, start_col)
        else:
            return Token(TokenType.INT_LIT, value, start_line, start_col)

    def _read_identifier(self) -> Optional[Token]:
        start_line, start_col = self.line, self.col
        result = []

        while self.pos < len(self.source):
            ch = self.source[self.pos]
            # Allow Myanmar Unicode range, ASCII alphanumeric, and underscore
            if (ch.isalnum() or ch == '_' or
                ('\u1000' <= ch <= '\u109F') or  # Myanmar Basic
                ('\uAA60' <= ch <= '\uAA7F') or  # Myanmar Extended-A
                ('\uA9E0' <= ch <= '\uA9FF') or  # Myanmar Extended-B
                ('\u200C' <= ch <= '\u200D')):   # Zero-width joiner/non-joiner
                result.append(self._advance())
            else:
                break

        if not result:
            return None

        value = ''.join(result)
        start_line_use = start_line

        # Check Myanmar keywords first
        if value in self.MYANMAR_KEYWORDS:
            return Token(self.MYANMAR_KEYWORDS[value], value, start_line_use, start_col)

        # Check English keywords
        if value in self.ENGLISH_KEYWORDS:
            return Token(self.ENGLISH_KEYWORDS[value], value, start_line_use, start_col)

        return Token(TokenType.IDENT, value, start_line_use, start_col)

    def _read_operator(self) -> Optional[Token]:
        ch = self._peek()
        start_line, start_col = self.line, self.col

        # Two-character operators
        two = self.source[self.pos:self.pos + 2] if self.pos + 1 < len(self.source) else ''

        if two == '**':
            self._advance(); self._advance()
            return Token(TokenType.POWER, '**', start_line, start_col)
        elif two == '++':
            self._advance(); self._advance()
            return Token(TokenType.INCREMENT, '++', start_line, start_col)
        elif two == '--':
            self._advance(); self._advance()
            return Token(TokenType.DECREMENT, '--', start_line, start_col)
        elif two == '==':
            self._advance(); self._advance()
            return Token(TokenType.EQ, '==', start_line, start_col)
        elif two == '!=':
            self._advance(); self._advance()
            return Token(TokenType.NEQ, '!=', start_line, start_col)
        elif two == '<=':
            self._advance(); self._advance()
            return Token(TokenType.LTE, '<=', start_line, start_col)
        elif two == '>=':
            self._advance(); self._advance()
            return Token(TokenType.GTE, '>=', start_line, start_col)
        elif two == '->':
            self._advance(); self._advance()
            return Token(TokenType.ARROW, '->', start_line, start_col)
        elif two == '::':
            self._advance(); self._advance()
            return Token(TokenType.DOUBLE_COLON, '::', start_line, start_col)

        # Single-character operators
        op_map = {
            '+': TokenType.PLUS, '-': TokenType.MINUS,
            '*': TokenType.STAR, '/': TokenType.SLASH,
            '%': TokenType.PERCENT, '=': TokenType.ASSIGN,
            '<': TokenType.LT, '>': TokenType.GT,
            '(': TokenType.LPAREN, ')': TokenType.RPAREN,
            '[': TokenType.LBRACKET, ']': TokenType.RBRACKET,
            '{': TokenType.LBRACE, '}': TokenType.RBRACE,
            ',': TokenType.COMMA, '.': TokenType.DOT,
            ':': TokenType.COLON, ';': TokenType.SEMICOLON,
            '?': TokenType.QUESTION,
        }

        if ch in op_map:
            self._advance()
            return Token(op_map[ch], ch, start_line, start_col)

        return None

    def tokenize(self) -> List[Token]:
        """Tokenize the entire MMC source with indentation tracking."""
        while self.pos < len(self.source):
            ch = self._peek()

            # Skip whitespace and handle indentation
            if ch in ' \t\r\n':
                self._skip_whitespace_and_indent()
                continue

            # Skip comments
            if ch == '#' or (ch == '/' and self._peek(1) == '/'):
                self._skip_comment()
                continue

            # String literals
            if ch in ('"', "'"):
                tok = self._read_string(ch)
                if tok:
                    self.tokens.append(tok)
                continue

            # Number literals
            if ch.isdigit():
                tok = self._read_number()
                if tok:
                    self.tokens.append(tok)
                continue

            # Identifiers and keywords
            if (ch.isalpha() or ch == '_' or
                '\u1000' <= ch <= '\u109F' or
                '\uAA60' <= ch <= '\uAA7F'):
                tok = self._read_identifier()
                if tok:
                    self.tokens.append(tok)
                continue

            # Operators and delimiters
            tok = self._read_operator()
            if tok:
                self.tokens.append(tok)
                continue

            # Unknown character
            self.errors.append(f"Unexpected character '{ch}' at L{self.line}:{self.col}")
            self._advance()

        # Emit remaining DEDENT tokens
        while len(self.indent_stack) > 1:
            self.indent_stack.pop()
            self.tokens.append(Token(TokenType.DEDENT, 'DEDENT', self.line, 0))

        self.tokens.append(Token(TokenType.EOF, '', self.line, self.col))
        return self.tokens

    def format_tokens(self) -> str:
        """Pretty-print all tokens for debugging."""
        lines = []
        for tok in self.tokens:
            if tok.type in (TokenType.NEWLINE, TokenType.EOF):
                continue
            lines.append(f"  {tok.type.name:20s} {tok.value!r:30s}  L{tok.line}:{tok.col}")
        return '\n'.join(lines)


def tokenize(source: str, filename: str = "<mmc>") -> List[Token]:
    """Convenience function: tokenize MMC source."""
    lexer = MMCLexer(source, filename)
    return lexer.tokenize()


if __name__ == "__main__":
    import sys

    # Test code
    test_code = """
    # MMC Test Program
    လိုလား အား = ၄၂
    လိုလား နာရီ = "မင်္ဂလာပါ"
    ပုံနှိပ် "Hello from MMC!"
    ပုံနှိပ် အား

    ကောင်းလျှင် အား > ၃၀
        ပုံနှိပ်း "Big number!"
    တိုင်းပဲ
        ပုံနှိပ်း "Small number"

    အတွက် ကိန်း အတိုင်း(၁၀)
        ပုံနှိပ် ကိန်း
    """

    tokens = tokenize(test_code, "test.mmc")
    print(f"=== MMC Lexer v2.0.0 ===")
    print(f"Tokens: {len(tokens)}")
    if tokens:
        lexer = MMCLexer(test_code)
        lexer.tokens = tokens
        print(lexer.format_tokens())
