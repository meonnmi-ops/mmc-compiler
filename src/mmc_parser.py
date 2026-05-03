#!/usr/bin/env python3
"""
MMC Parser - AST Generator for MMC Programming Language
=========================================================
Produces Abstract Syntax Tree from token stream.
Supports full MMC syntax: variables, control flow, functions, classes.

Version: 2.0.0
License: MIT
Author: MMC Compiler Team / Nyanlin-AI
"""

from dataclasses import dataclass, field
from typing import List, Optional, Any, Union
from enum import Enum, auto

from mmc_lexer import Token, TokenType


# ============================================================================
#  AST Node Types
# ============================================================================

class ASTNodeType(Enum):
    """AST node type identifiers"""
    # Literals
    INT_LIT = auto()
    FLOAT_LIT = auto()
    STRING_LIT = auto()
    BOOL_LIT = auto()
    NONE_LIT = auto()

    # Expressions
    IDENT = auto()
    BINOP = auto()
    UNOP = auto()
    CALL = auto()
    MEMBER_ACCESS = auto()
    INDEX_ACCESS = auto()
    ARRAY_LIT = auto()
    DICT_LIT = auto()
    TERNARY = auto()
    CAST = auto()

    # Statements
    VAR_DECLARE = auto()
    ASSIGN = auto()
    AUG_ASSIGN = auto()
    PRINT_STMT = auto()
    INPUT_STMT = auto()
    IF_STMT = auto()
    WHILE_STMT = auto()
    FOR_STMT = auto()
    FOR_RANGE = auto()
    FOR_EACH = auto()
    BREAK_STMT = auto()
    CONTINUE_STMT = auto()
    RETURN_STMT = auto()
    PASS_STMT = auto()
    ASSERT_STMT = auto()
    EXPR_STMT = auto()

    # Top-level
    FUNC_DEF = auto()
    CLASS_DEF = auto()
    IMPORT_STMT = auto()
    GLOBAL_STMT = auto()
    TRY_CATCH = auto()
    LAMBDA_EXPR = auto()
    BLOCK = auto()
    PROGRAM = auto()


@dataclass
class ASTNode:
    """Base AST node"""
    type: ASTNodeType
    line: int = 0
    col: int = 0
    children: List['ASTNode'] = field(default_factory=list)
    value: Any = None
    name: str = ""
    annotation: str = ""  # type annotation

    def __repr__(self):
        if self.children:
            return f"AST({self.type.name}, children={len(self.children)})"
        return f"AST({self.type.name}, value={self.value!r})"


# ============================================================================
#  Parser
# ============================================================================

class MMCParser:
    """
    Recursive descent parser for MMC language.

    Grammar (simplified):
        program     := statement*
        statement   := var_decl | if_stmt | while_stmt | for_stmt | func_def
                     | class_def | print_stmt | return_stmt | expr_stmt
        var_decl    := (VAR_DECLARE|type_kw) IDENT [annotation] [= expr]
        if_stmt     := IF expr block (ELIF expr block)* (ELSE block)?
        while_stmt  := WHILE expr block
        for_stmt    := FOR IDENT IN expr block
        func_def    := FUNC IDENT '(' params ')' [-> type] block
        print_stmt  := PRINT expr_list
        expr        := assign_expr
        assign_expr := ternary (('=' | '+=' | '-=' | ...) IDENT)*
        ternary     := or_expr ('?' expr ':' expr)?
        or_expr     := and_expr ((OR_KW|OR) and_expr)*
        and_expr    := not_expr ((AND_KW|AND) not_expr)*
        not_expr    := (NOT_KW|NOT) not_expr | comparison
        comparison  := addition ((EQ|NEQ|LT|GT|LTE|GTE) addition)*
        addition    := multiplication ((PLUS|MINUS) multiplication)*
        multiplication := unary ((STAR|SLASH|PERCENT) unary)*
        unary       := (MINUS|NOT) unary | postfix
        postfix     := primary ('(' args ')' | '[' expr ']' | '.' IDENT)*
        primary     := INT_LIT | FLOAT_LIT | STRING_LIT | BOOL_LIT | NONE_LIT
                     | IDENT | '(' expr ')' | '[' expr_list ']' | '{' dict_entries '}'
    """

    def __init__(self, tokens: List[Token]):
        self.tokens = tokens
        self.pos = 0
        self.errors: List[str] = []

    def _cur(self) -> Token:
        if self.pos < len(self.tokens):
            return self.tokens[self.pos]
        return Token(TokenType.EOF, '', -1, -1)

    def _peek(self, offset: int = 0) -> Token:
        idx = self.pos + offset
        if idx < len(self.tokens):
            return self.tokens[idx]
        return Token(TokenType.EOF, '', -1, -1)

    def _advance(self) -> Token:
        tok = self._cur()
        self.pos += 1
        return tok

    def _expect(self, tt: TokenType) -> Optional[Token]:
        tok = self._cur()
        if tok.type == tt:
            return self._advance()
        self.errors.append(f"Expected {tt.name}, got {tok.type.name} ({tok.value!r}) at L{tok.line}:{tok.col}")
        return None

    def _match(self, *types: TokenType) -> Optional[Token]:
        if self._cur().type in types:
            return self._advance()
        return None

    def _skip_newlines(self):
        while self._cur().type == TokenType.NEWLINE:
            self._advance()

    # --- Top-level ---

    def parse(self) -> ASTNode:
        """Parse the entire program."""
        stmts = []
        self._skip_newlines()

        while self._cur().type != TokenType.EOF:
            stmt = self._parse_statement()
            if stmt:
                stmts.append(stmt)
            self._skip_newlines()

        return ASTNode(ASTNodeType.PROGRAM, children=stmts)

    # --- Statements ---

    def _parse_block(self) -> ASTNode:
        """Parse a block of statements (INDENT/DEDENT or braced)."""
        stmts = []
        # Handle brace-delimited blocks
        if self._match(TokenType.LBRACE):
            self._skip_newlines()
            while self._cur().type != TokenType.RBRACE and self._cur().type != TokenType.EOF:
                stmt = self._parse_statement()
                if stmt:
                    stmts.append(stmt)
                self._skip_newlines()
            self._expect(TokenType.RBRACE)
        elif self._match(TokenType.INDENT):
            # Indentation-based block: INDENT ... DEDENT
            while self._cur().type not in (TokenType.DEDENT, TokenType.EOF):
                stmt = self._parse_statement()
                if stmt:
                    stmts.append(stmt)
            self._expect(TokenType.DEDENT)
        else:
            # Single-statement block (no braces, no indent - just next statement)
            stmt = self._parse_statement()
            if stmt:
                stmts.append(stmt)
        return ASTNode(ASTNodeType.BLOCK, children=stmts)

    def _parse_statement(self) -> Optional[ASTNode]:
        """Parse a single statement."""
        tok = self._cur()

        # Variable declaration
        if tok.type in (TokenType.VAR_DECLARE, TokenType.INT_TYPE, TokenType.FLOAT_TYPE,
                        TokenType.STR_TYPE, TokenType.BOOL_TYPE, TokenType.ARRAY_TYPE,
                        TokenType.DICT_TYPE, TokenType.VOID_TYPE, TokenType.CONST):
            return self._parse_var_decl()

        # Function definition
        if tok.type == TokenType.FUNC:
            return self._parse_func_def()

        # Class definition
        if tok.type == TokenType.CLASS:
            return self._parse_class_def()

        # Import
        if tok.type == TokenType.IMPORT:
            return self._parse_import()

        # If statement
        if tok.type == TokenType.IF:
            return self._parse_if_stmt()

        # While statement
        if tok.type == TokenType.WHILE:
            return self._parse_while_stmt()

        # For statement
        if tok.type == TokenType.FOR:
            return self._parse_for_stmt()

        # Print
        if tok.type in (TokenType.PRINT, TokenType.PRINTLN):
            return self._parse_print_stmt()

        # Input
        if tok.type == TokenType.INPUT:
            return self._parse_input_stmt()

        # Return
        if tok.type == TokenType.RETURN:
            return self._parse_return_stmt()

        # Break
        if tok.type == TokenType.BREAK:
            self._advance()
            return ASTNode(ASTNodeType.BREAK_STMT, line=tok.line, col=tok.col)

        # Continue
        if tok.type == TokenType.CONTINUE:
            self._advance()
            return ASTNode(ASTNodeType.CONTINUE_STMT, line=tok.line, col=tok.col)

        # Pass
        if tok.type == TokenType.PASS:
            self._advance()
            return ASTNode(ASTNodeType.PASS_STMT, line=tok.line, col=tok.col)

        # Assert
        if tok.type == TokenType.ASSERT:
            return self._parse_assert_stmt()

        # Try-catch
        if tok.type == TokenType.TRY:
            return self._parse_try_catch()

        # Global
        if tok.type == TokenType.GLOBAL:
            return self._parse_global_stmt()

        # Expression statement
        if tok.type not in (TokenType.EOF, TokenType.RBRACE, TokenType.DEDENT):
            return self._parse_expr_stmt()

        return None

    def _parse_var_decl(self) -> Optional[ASTNode]:
        tok = self._advance()  # consume var/type keyword

        is_const = (tok.type == TokenType.CONST)
        var_type = tok.value if tok.type not in (TokenType.VAR_DECLARE,) else ""

        if self._cur().type != TokenType.IDENT:
            self.errors.append(f"Expected identifier after variable declaration at L{tok.line}:{tok.col}")
            return None

        name_tok = self._advance()
        annotation = ""

        # Type annotation: IDENT : TYPE
        if self._match(TokenType.COLON):
            ann_tok = self._advance()
            annotation = ann_tok.value

        # Initial value
        init_expr = None
        if self._match(TokenType.ASSIGN):
            init_expr = self._parse_expression()

        node = ASTNode(ASTNodeType.VAR_DECLARE, line=tok.line, col=tok.col,
                       name=name_tok.value, annotation=annotation or var_type)
        node.value = init_expr
        return node

    def _parse_func_def(self) -> Optional[ASTNode]:
        tok = self._advance()  # consume 'func'

        if self._cur().type != TokenType.IDENT:
            self.errors.append(f"Expected function name at L{tok.line}:{tok.col}")
            return None

        name_tok = self._advance()
        params = []
        return_type = ""

        # Parameters
        if self._expect(TokenType.LPAREN):
            while self._cur().type != TokenType.RPAREN and self._cur().type != TokenType.EOF:
                if self._cur().type == TokenType.IDENT:
                    p_name = self._advance().value
                    p_type = ""
                    if self._match(TokenType.COLON):
                        p_type = self._advance().value
                    params.append((p_name, p_type))

                    if not self._match(TokenType.COMMA):
                        break
                else:
                    self._advance()  # skip unexpected
            self._expect(TokenType.RPAREN)

        # Return type
        if self._match(TokenType.ARROW):
            ret_tok = self._advance()
            return_type = ret_tok.value

        body = self._parse_block()

        node = ASTNode(ASTNodeType.FUNC_DEF, line=tok.line, col=tok.col,
                       name=name_tok.value, annotation=return_type,
                       value=params, children=[body])
        return node

    def _parse_class_def(self) -> Optional[ASTNode]:
        tok = self._advance()  # consume 'class'

        if self._cur().type != TokenType.IDENT:
            self.errors.append(f"Expected class name at L{tok.line}:{tok.col}")
            return None

        name_tok = self._advance()
        parent = ""

        if self._match(TokenType.EXTENDS):
            parent_tok = self._advance()
            parent = parent_tok.value

        body = self._parse_block()

        node = ASTNode(ASTNodeType.CLASS_DEF, line=tok.line, col=tok.col,
                       name=name_tok.value, annotation=parent, children=[body])
        return node

    def _parse_import(self) -> Optional[ASTNode]:
        tok = self._advance()
        module = ""

        if self._cur().type in (TokenType.STRING_LIT, TokenType.IDENT):
            module = self._advance().value

        alias = ""
        if self._match(TokenType.AS):
            alias = self._advance().value

        node = ASTNode(ASTNodeType.IMPORT_STMT, line=tok.line, col=tok.col,
                       name=module, annotation=alias)
        return node

    def _parse_if_stmt(self) -> Optional[ASTNode]:
        tok = self._advance()  # consume 'if'

        cond = self._parse_expression()
        then_block = self._parse_block()

        node = ASTNode(ASTNodeType.IF_STMT, line=tok.line, col=tok.col,
                       value=cond, children=[then_block])

        # elif branches
        while self._cur().type == TokenType.ELIF:
            self._advance()
            elif_cond = self._parse_expression()
            elif_block = self._parse_block()
            elif_node = ASTNode(ASTNodeType.IF_STMT, line=tok.line, col=tok.col,
                               value=elif_cond, children=[elif_block])
            node.children.append(elif_node)

        # else branch
        if self._match(TokenType.ELSE):
            else_block = self._parse_block()
            else_node = ASTNode(ASTNodeType.IF_STMT, line=tok.line, col=tok.col,
                               value=None, children=[else_block])
            node.children.append(else_node)

        return node

    def _parse_while_stmt(self) -> Optional[ASTNode]:
        tok = self._advance()  # consume 'while'

        cond = self._parse_expression()
        body = self._parse_block()

        return ASTNode(ASTNodeType.WHILE_STMT, line=tok.line, col=tok.col,
                       value=cond, children=[body])

    def _parse_for_stmt(self) -> Optional[ASTNode]:
        tok = self._advance()  # consume 'for'

        if self._cur().type != TokenType.IDENT:
            self.errors.append(f"Expected loop variable at L{tok.line}:{tok.col}")
            return None

        var_name = self._advance().value

        if not self._expect(TokenType.IN):
            return None

        # Check for range expression: for i in range(...)
        iterable = self._parse_expression()

        # Detect if iterable is a range() call
        if iterable.type == ASTNodeType.CALL and iterable.name == "range":
            node = ASTNode(ASTNodeType.FOR_RANGE, line=tok.line, col=tok.col,
                          name=var_name, value=iterable, children=[self._parse_block()])
        else:
            node = ASTNode(ASTNodeType.FOR_EACH, line=tok.line, col=tok.col,
                          name=var_name, value=iterable, children=[self._parse_block()])

        return node

    def _parse_print_stmt(self) -> Optional[ASTNode]:
        tok = self._advance()
        is_newline = (tok.type == TokenType.PRINTLN)

        args = []
        if self._cur().type not in (TokenType.EOF, TokenType.RBRACE,
                                     TokenType.ELIF, TokenType.ELSE, TokenType.CATCH,
                                     TokenType.DEDENT, TokenType.INDENT):
            args.append(self._parse_expression())
            while self._match(TokenType.COMMA):
                args.append(self._parse_expression())

        node = ASTNode(ASTNodeType.PRINT_STMT, line=tok.line, col=tok.col)
        node.value = args
        node.annotation = "newline" if is_newline else ""
        return node

    def _parse_input_stmt(self) -> Optional[ASTNode]:
        tok = self._advance()

        prompt = ASTNode(ASTNodeType.STRING_LIT, value="")
        if self._cur().type not in (TokenType.EOF, TokenType.DEDENT, TokenType.INDENT):
            prompt = self._parse_expression()

        return ASTNode(ASTNodeType.INPUT_STMT, line=tok.line, col=tok.col, value=prompt)

    def _parse_return_stmt(self) -> Optional[ASTNode]:
        tok = self._advance()

        expr = None
        if self._cur().type not in (TokenType.EOF, TokenType.RBRACE,
                                     TokenType.DEDENT, TokenType.INDENT):
            expr = self._parse_expression()

        return ASTNode(ASTNodeType.RETURN_STMT, line=tok.line, col=tok.col, value=expr)

    def _parse_assert_stmt(self) -> Optional[ASTNode]:
        tok = self._advance()
        expr = self._parse_expression()
        msg = ASTNode(ASTNodeType.STRING_LIT, value="Assertion failed")
        if self._match(TokenType.COMMA):
            msg = self._parse_expression()
        node = ASTNode(ASTNodeType.ASSERT_STMT, line=tok.line, col=tok.col,
                       value=expr, children=[msg])
        return node

    def _parse_try_catch(self) -> Optional[ASTNode]:
        tok = self._advance()  # consume 'try'
        try_block = self._parse_block()

        catch_blocks = []
        finally_block = None

        while self._cur().type == TokenType.CATCH:
            self._advance()
            exc_name = ""
            if self._cur().type == TokenType.IDENT:
                exc_name = self._advance().value
            catch_block = self._parse_block()
            catch_blocks.append((exc_name, catch_block))

        if self._cur().type == TokenType.FINALLY:
            self._advance()
            finally_block = self._parse_block()

        node = ASTNode(ASTNodeType.TRY_CATCH, line=tok.line, col=tok.col,
                       value=try_block)
        node.name = str(len(catch_blocks))
        for exc_name, catch_block in catch_blocks:
            cb_node = ASTNode(ASTNodeType.IF_STMT, name=exc_name, children=[catch_block])
            node.children.append(cb_node)
        if finally_block:
            node.children.append(finally_block)

        return node

    def _parse_global_stmt(self) -> Optional[ASTNode]:
        tok = self._advance()
        names = []
        if self._cur().type == TokenType.IDENT:
            names.append(self._advance().value)
            while self._match(TokenType.COMMA):
                names.append(self._advance().value)
        node = ASTNode(ASTNodeType.GLOBAL_STMT, line=tok.line, col=tok.col)
        node.value = names
        return node

    def _parse_expr_stmt(self) -> Optional[ASTNode]:
        expr = self._parse_expression()
        return ASTNode(ASTNodeType.EXPR_STMT, line=expr.line, col=expr.col, value=expr)

    # --- Expressions (precedence climbing) ---

    def _parse_expression(self) -> ASTNode:
        return self._parse_assign_expr()

    def _parse_assign_expr(self) -> ASTNode:
        expr = self._parse_ternary()

        # Simple assignment
        if self._match(TokenType.ASSIGN):
            right = self._parse_assign_expr()
            if expr.type == ASTNodeType.IDENT:
                return ASTNode(ASTNodeType.ASSIGN, line=expr.line, col=expr.col,
                              name=expr.name, value=right)
            return ASTNode(ASTNodeType.ASSIGN, line=expr.line, col=expr.col, value=right)

        # Augmented assignment detection: x + = y  (pattern: ident BINOP = expr)
        if (self._cur().type == TokenType.ASSIGN and expr.type == ASTNodeType.BINOP
                and expr.value in ('+', '-', '*', '/', '%')):
            self._advance()  # consume '='
            right = self._parse_assign_expr()
            target = expr.children[0]
            if target.type == ASTNodeType.IDENT:
                return ASTNode(ASTNodeType.AUG_ASSIGN, line=expr.line, col=expr.col,
                              name=target.name, value=right, annotation=expr.value)

        return expr

    def _parse_ternary(self) -> ASTNode:
        expr = self._parse_or_expr()
        # C-style ternary: cond ? then : else
        if self._match(TokenType.QUESTION):
            then_expr = self._parse_expression()
            self._expect(TokenType.COLON)
            else_expr = self._parse_ternary()
            return ASTNode(ASTNodeType.TERNARY, line=expr.line, col=expr.col,
                          children=[expr, then_expr, else_expr])
        return expr

    def _parse_or_expr(self) -> ASTNode:
        left = self._parse_and_expr()
        while self._cur().type in (TokenType.OR_KW, TokenType.OR):
            op = self._advance().value
            right = self._parse_and_expr()
            left = ASTNode(ASTNodeType.BINOP, line=left.line, col=left.col,
                          value='||', children=[left, right])
        return left

    def _parse_and_expr(self) -> ASTNode:
        left = self._parse_not_expr()
        while self._cur().type in (TokenType.AND_KW, TokenType.AND):
            op = self._advance().value
            right = self._parse_not_expr()
            left = ASTNode(ASTNodeType.BINOP, line=left.line, col=left.col,
                          value='&&', children=[left, right])
        return left

    def _parse_not_expr(self) -> ASTNode:
        if self._cur().type in (TokenType.NOT_KW, TokenType.NOT):
            tok = self._advance()
            operand = self._parse_not_expr()
            return ASTNode(ASTNodeType.UNOP, line=tok.line, col=tok.col,
                          value='!', children=[operand])
        return self._parse_comparison()

    def _parse_comparison(self) -> ASTNode:
        left = self._parse_addition()
        comp_ops = {TokenType.EQ: '==', TokenType.NEQ: '!=',
                    TokenType.LT: '<', TokenType.GT: '>',
                    TokenType.LTE: '<=', TokenType.GTE: '>='}

        while self._cur().type in comp_ops:
            op_tok = self._advance()
            op = comp_ops[op_tok.type]
            right = self._parse_addition()
            left = ASTNode(ASTNodeType.BINOP, line=left.line, col=left.col,
                          value=op, children=[left, right])
        return left

    def _parse_addition(self) -> ASTNode:
        left = self._parse_multiplication()
        while self._cur().type in (TokenType.PLUS, TokenType.MINUS):
            op = self._advance().value
            right = self._parse_multiplication()
            left = ASTNode(ASTNodeType.BINOP, line=left.line, col=left.col,
                          value=op, children=[left, right])
        return left

    def _parse_multiplication(self) -> ASTNode:
        left = self._parse_unary()
        while self._cur().type in (TokenType.STAR, TokenType.SLASH, TokenType.PERCENT):
            op = self._advance().value
            right = self._parse_unary()
            left = ASTNode(ASTNodeType.BINOP, line=left.line, col=left.col,
                          value=op, children=[left, right])
        return left

    def _parse_unary(self) -> ASTNode:
        if self._cur().type == TokenType.MINUS:
            tok = self._advance()
            operand = self._parse_unary()
            return ASTNode(ASTNodeType.UNOP, line=tok.line, col=tok.col,
                          value='-', children=[operand])
        if self._cur().type == TokenType.INCREMENT:
            tok = self._advance()
            operand = self._parse_postfix()
            return ASTNode(ASTNodeType.UNOP, line=tok.line, col=tok.col,
                          value='++', children=[operand])
        if self._cur().type == TokenType.DECREMENT:
            tok = self._advance()
            operand = self._parse_postfix()
            return ASTNode(ASTNodeType.UNOP, line=tok.line, col=tok.col,
                          value='--', children=[operand])
        return self._parse_postfix()

    def _parse_postfix(self) -> ASTNode:
        expr = self._parse_primary()

        while True:
            # Function call: expr(args)
            if self._cur().type == TokenType.LPAREN:
                self._advance()
                args = []
                if self._cur().type != TokenType.RPAREN:
                    args.append(self._parse_expression())
                    while self._match(TokenType.COMMA):
                        args.append(self._parse_expression())
                self._expect(TokenType.RPAREN)
                call_name = expr.name if expr.type == ASTNodeType.IDENT else ""
                expr = ASTNode(ASTNodeType.CALL, line=expr.line, col=expr.col,
                              name=call_name, value=args, children=[expr])
            # Index access: expr[index]
            elif self._cur().type == TokenType.LBRACKET:
                self._advance()
                index = self._parse_expression()
                self._expect(TokenType.RBRACKET)
                expr = ASTNode(ASTNodeType.INDEX_ACCESS, line=expr.line, col=expr.col,
                              value=index, children=[expr])
            # Member access: expr.member
            elif self._cur().type == TokenType.DOT:
                self._advance()
                member = self._advance()
                expr = ASTNode(ASTNodeType.MEMBER_ACCESS, line=expr.line, col=expr.col,
                              name=member.value, children=[expr])
            else:
                break

        return expr

    def _parse_primary(self) -> ASTNode:
        tok = self._cur()

        # Integer literal
        if tok.type == TokenType.INT_LIT:
            self._advance()
            return ASTNode(ASTNodeType.INT_LIT, line=tok.line, col=tok.col,
                          value=int(tok.value))

        # Float literal
        if tok.type == TokenType.FLOAT_LIT:
            self._advance()
            return ASTNode(ASTNodeType.FLOAT_LIT, line=tok.line, col=tok.col,
                          value=float(tok.value))

        # String literal
        if tok.type == TokenType.STRING_LIT:
            self._advance()
            return ASTNode(ASTNodeType.STRING_LIT, line=tok.line, col=tok.col,
                          value=tok.value)

        # Boolean
        if tok.type == TokenType.TRUE:
            self._advance()
            return ASTNode(ASTNodeType.BOOL_LIT, line=tok.line, col=tok.col, value=True)
        if tok.type == TokenType.FALSE:
            self._advance()
            return ASTNode(ASTNodeType.BOOL_LIT, line=tok.line, col=tok.col, value=False)

        # None
        if tok.type in (TokenType.NONE_KW, TokenType.NONE_LIT):
            self._advance()
            return ASTNode(ASTNodeType.NONE_LIT, line=tok.line, col=tok.col, value=None)

        # Identifier
        if tok.type == TokenType.IDENT:
            self._advance()
            return ASTNode(ASTNodeType.IDENT, line=tok.line, col=tok.col, name=tok.value)

        # Parenthesized expression
        if tok.type == TokenType.LPAREN:
            self._advance()
            expr = self._parse_expression()
            self._expect(TokenType.RPAREN)
            return expr

        # Array literal
        if tok.type == TokenType.LBRACKET:
            self._advance()
            elements = []
            if self._cur().type != TokenType.RBRACKET:
                elements.append(self._parse_expression())
                while self._match(TokenType.COMMA):
                    elements.append(self._parse_expression())
            self._expect(TokenType.RBRACKET)
            return ASTNode(ASTNodeType.ARRAY_LIT, line=tok.line, col=tok.col,
                          value=elements)

        # Dict literal or block
        if tok.type == TokenType.LBRACE:
            # Could be a dict literal: {key: value, ...}
            # Peek ahead to check for ':'
            if self._peek(1).type == TokenType.COLON and self._peek(2).type != TokenType.COLON:
                return self._parse_dict_literal()
            # Otherwise it's a block (handled by caller)

        # Error
        self.errors.append(f"Unexpected token {tok.type.name} ({tok.value!r}) at L{tok.line}:{tok.col}")
        self._advance()
        return ASTNode(ASTNodeType.NONE_LIT, line=tok.line, col=tok.col, value=None)

    def _parse_dict_literal(self) -> ASTNode:
        tok = self._advance()  # consume '{'
        entries = []
        while self._cur().type != TokenType.RBRACE and self._cur().type != TokenType.EOF:
            key = self._parse_expression()
            self._expect(TokenType.COLON)
            value = self._parse_expression()
            entries.append((key, value))
            if not self._match(TokenType.COMMA):
                break
        self._expect(TokenType.RBRACE)
        return ASTNode(ASTNodeType.DICT_LIT, line=tok.line, col=tok.col, value=entries)


def parse(tokens: List[Token]) -> ASTNode:
    """Convenience function: parse token stream to AST."""
    parser = MMCParser(tokens)
    return parser.parse()


def print_ast(node: ASTNode, indent: int = 0) -> str:
    """Pretty-print AST for debugging."""
    prefix = "  " * indent
    lines = []

    if node.type == ASTNodeType.PROGRAM:
        lines.append(f"{prefix}Program:")
        for child in node.children:
            lines.append(print_ast(child, indent + 1))
        return '\n'.join(lines)

    if node.type == ASTNodeType.BLOCK:
        for child in node.children:
            lines.append(print_ast(child, indent))
        return '\n'.join(lines)

    if node.type == ASTNodeType.INT_LIT:
        lines.append(f"{prefix}IntLit({node.value})")
    elif node.type == ASTNodeType.FLOAT_LIT:
        lines.append(f"{prefix}FloatLit({node.value})")
    elif node.type == ASTNodeType.STRING_LIT:
        lines.append(f"{prefix}StrLit({node.value!r})")
    elif node.type == ASTNodeType.BOOL_LIT:
        lines.append(f"{prefix}BoolLit({node.value})")
    elif node.type == ASTNodeType.NONE_LIT:
        lines.append(f"{prefix}NoneLit")
    elif node.type == ASTNodeType.IDENT:
        lines.append(f"{prefix}Ident({node.name})")
    elif node.type == ASTNodeType.BINOP:
        lines.append(f"{prefix}BinOp({node.value})")
        for child in node.children:
            lines.append(print_ast(child, indent + 1))
    elif node.type == ASTNodeType.UNOP:
        lines.append(f"{prefix}UnOp({node.value})")
        for child in node.children:
            lines.append(print_ast(child, indent + 1))
    elif node.type == ASTNodeType.CALL:
        args_str = ", ".join(print_ast(a, 0).strip() for a in (node.value or []))
        lines.append(f"{prefix}Call({node.name}, [{args_str}])")
    elif node.type == ASTNodeType.VAR_DECLARE:
        init = print_ast(node.value, 0).strip() if node.value else "None"
        lines.append(f"{prefix}VarDecl({node.name}: {node.annotation} = {init})")
    elif node.type == ASTNodeType.ASSIGN:
        val = print_ast(node.value, 0).strip() if node.value else "None"
        lines.append(f"{prefix}Assign({node.name} = {val})")
    elif node.type == ASTNodeType.PRINT_STMT:
        args_str = ", ".join(print_ast(a, 0).strip() for a in (node.value or []))
        suffix = " + newline" if node.annotation == "newline" else ""
        lines.append(f"{prefix}Print([{args_str}]{suffix})")
    elif node.type == ASTNodeType.IF_STMT:
        if node.value:
            lines.append(f"{prefix}If({print_ast(node.value, 0).strip()}):")
        else:
            lines.append(f"{prefix}Else:")
        for child in node.children:
            lines.append(print_ast(child, indent + 1))
    elif node.type == ASTNodeType.WHILE_STMT:
        lines.append(f"{prefix}While({print_ast(node.value, 0).strip()}):")
        for child in node.children:
            lines.append(print_ast(child, indent + 1))
    elif node.type == ASTNodeType.FOR_RANGE:
        lines.append(f"{prefix}ForRange({node.name} in {node.name}):")
        for child in node.children:
            lines.append(print_ast(child, indent + 1))
    elif node.type == ASTNodeType.FOR_EACH:
        lines.append(f"{prefix}ForEach({node.name} in iter):")
        for child in node.children:
            lines.append(print_ast(child, indent + 1))
    elif node.type == ASTNodeType.FUNC_DEF:
        params = ", ".join(f"{p[0]}: {p[1]}" for p in (node.value or []))
        lines.append(f"{prefix}FuncDef({node.name}({params}) -> {node.annotation}):")
        for child in node.children:
            lines.append(print_ast(child, indent + 1))
    elif node.type == ASTNodeType.RETURN_STMT:
        val = print_ast(node.value, 0).strip() if node.value else "None"
        lines.append(f"{prefix}Return({val})")
    elif node.type == ASTNodeType.EXPR_STMT:
        lines.append(f"{prefix}ExprStmt({print_ast(node.value, 0).strip()})")
    else:
        lines.append(f"{prefix}{node.type.name}({node.value})")

    return '\n'.join(lines)


if __name__ == "__main__":
    from mmc_lexer import tokenize

    test_code = """
    လိုလား x = ၁၀
    လိုလား y = ၂၀
    ပုံနှိပ် x + y
    ပုံနှိပ် "Hello MMC!"
    """

    tokens = tokenize(test_code, "test.mmc")
    ast = parse(tokens)
    print(print_ast(ast))
