#!/usr/bin/env python3
"""
MMC LLVM Code Generator - AST to LLVM IR
==========================================
Compiles MMC AST into valid LLVM IR (text format).
Outputs .ll files that can be compiled with: llc output.ll -o output.s && gcc output.s -o program

Supports: int, float, string, bool, arrays, dicts,
          variables, arithmetic, comparisons, if/else, while, for, functions,
          printf, user-defined functions, string concatenation.

Version: 2.0.0
License: MIT
Author: MMC Compiler Team / Nyanlin-AI
"""

import sys
from typing import List, Optional, Dict, Set, Tuple

from mmc_parser import ASTNode, ASTNodeType, parse
from mmc_lexer import tokenize, TokenType


class LLVMScope:
    """Variable scope for LLVM IR generation (name -> (type, alloca_reg))"""

    def __init__(self, parent=None):
        self.parent: Optional[LLVMScope] = parent
        self.vars: Dict[str, Tuple[str, str]] = {}  # name -> (llvm_type, reg_name)

    def define(self, name: str, llvm_type: str, reg: str):
        self.vars[name] = (llvm_type, reg)

    def lookup(self, name: str) -> Optional[Tuple[str, str]]:
        if name in self.vars:
            return self.vars[name]
        if self.parent:
            return self.parent.lookup(name)
        return None

    def child(self):
        return LLVMScope(parent=self)


class MMCCodegenLLVM:
    """
    Generates LLVM IR from MMC AST.

    Design:
    - All MMC values are represented as i64 (int/bool/pointer/none)
    - Floats use double (f64)
    - Strings are pointers to global constants
    - Functions use SSA form with alloca for mutable variables
    - printf is declared externally for output
    """

    def __init__(self):
        self.output: List[str] = []
        self.reg_counter = 0
        self.label_counter = 0
        self.global_strings: Dict[str, str] = {}  # string_value -> @.str.N
        self.string_counter = 0
        self.functions: Dict[str, ASTNode] = {}
        self.scope = LLVMScope()
        self.current_function = "main"
        self.str_constants: List[str] = []

    # --- Helpers ---

    def _new_reg(self, prefix: str = "%t") -> str:
        self.reg_counter += 1
        return f"{prefix}{self.reg_counter}"

    def _new_label(self, name: str = "L") -> str:
        self.label_counter += 1
        return f"{name}{self.label_counter}"

    def _emit(self, line: str = ""):
        self.output.append(line)

    def _get_global_str(self, value: str) -> str:
        """Get or create a global string constant for the given value."""
        if value not in self.global_strings:
            self.string_counter += 1
            name = f"@.str.{self.string_counter}"
            self.global_strings[value] = name
            # Escape string for LLVM IR
            escaped = value.replace('\\', '\\5c').replace('"', '\\22').replace('\n', '\\0a').replace('\t', '\\09').replace('%', '%')
            # Actually, LLVM uses \xx for escapes
            escaped = ''
            for ch in value:
                code = ord(ch)
                if ch == '\\':
                    escaped += '\\5c'
                elif ch == '"':
                    escaped += '\\22'
                elif ch == '\n':
                    escaped += '\\0a'
                elif ch == '\t':
                    escaped += '\\09'
                elif ch == '%':
                    escaped += '%%'  # No, printf format char - escape to 25
                    escaped = escaped[:-2] + '\\25'
                elif code < 32 or code > 126:
                    if 0x1000 <= code <= 0x109F:
                        # Myanmar chars are UTF-8 multi-byte in LLVM
                        for b in ch.encode('utf-8'):
                            escaped += f'\\{b:02x}'
                    else:
                        escaped += ch
                else:
                    escaped += ch
            self.str_constants.append(f'  {name} = private unnamed_addr constant [{len(value) + 1} x i8] c"{escaped}\\00"')
        return self.global_strings[value]

    def _get_printf_format(self, fmt_type: str) -> str:
        """Get printf format string for a given type."""
        formats = {
            'i64': '%ld\\0a',
            'i32': '%d\\0a',
            'double': '%f\\0a',
            'float': '%f\\0a',
            'bool_true': 'true\\0a',
            'bool_false': 'false\\0a',
            'none': 'None\\0a',
            'str': '%s\\0a',
        }
        return self._get_global_str(formats.get(fmt_type, '%s\\0a'))

    def _determine_type(self, node: ASTNode) -> str:
        """Determine the LLVM type of an AST node."""
        if node.type == ASTNodeType.INT_LIT:
            return 'i64'
        elif node.type == ASTNodeType.FLOAT_LIT:
            return 'double'
        elif node.type == ASTNodeType.STRING_LIT:
            return 'i8*'
        elif node.type == ASTNodeType.BOOL_LIT:
            return 'i1'
        elif node.type == ASTNodeType.NONE_LIT:
            return 'i64'
        elif node.type == ASTNodeType.IDENT:
            result = self.scope.lookup(node.name)
            if result:
                return result[0]
            return 'i64'
        elif node.type == ASTNodeType.BINOP:
            if node.value in ('+', '-', '*', '/', '%'):
                left_type = self._determine_type(node.children[0])
                if left_type == 'double':
                    return 'double'
                return 'i64'
            return 'i64'
        elif node.type == ASTNodeType.UNOP:
            if node.value == '-':
                return self._determine_type(node.children[0])
            return 'i64'
        elif node.type == ASTNodeType.CALL:
            return 'i64'  # default return type
        elif node.type == ASTNodeType.ARRAY_LIT:
            return 'i8*'
        elif node.type == ASTNodeType.DICT_LIT:
            return 'i8*'
        return 'i64'

    # --- Code Generation ---

    def generate(self, ast: ASTNode) -> str:
        """Generate complete LLVM IR from AST."""
        # First pass: collect all function definitions
        for child in ast.children:
            if child.type == ASTNodeType.FUNC_DEF:
                self.functions[child.name] = child

        # Generate header
        self._emit('; ======================================================')
        self._emit('; MMC Compiler - LLVM IR Output')
        self._emit('; Generated by MMC Codegen LLVM v2.0.0')
        self._emit('; ======================================================')
        self._emit()

        # Target triple (auto-detect or default to aarch64-linux-android for Termux)
        self._emit('target triple = "aarch64-unknown-linux-android"')
        self._emit('target datalayout = "e-m:e-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128"')
        self._emit()

        # External function declarations
        self._emit('; --- External Declarations ---')
        self._emit('declare i32 @printf(i8*, ...)')

        # String helper declarations
        self._emit('declare i32 @puts(i8*)')
        self._emit('declare i64 @strlen(i8*)')
        self._emit('declare i8* @malloc(i64)')
        self._emit('declare void @free(i8*)')

        # Math declarations
        self._emit('declare double @pow(double, double)')
        self._emit('declare double @fabs(double)')
        self._emit('declare void @exit(i32)')
        self._emit()

        # String concatenation helper
        self._emit('; --- String Concatenation Helper ---')
        self._emit('define i8* @mmc_strcat(i8* %a, i8* %b) {')
        self._emit('  %la = call i64 @strlen(i8* %a)')
        self._emit('  %lb = call i64 @strlen(i8* %b)')
        self._emit('  %total = add i64 %la, %lb')
        self._emit('  %total1 = add i64 %total, 1')
        self._emit('  %buf = call i8* @malloc(i64 %total1)')
        self._emit('  br label %copy_a')
        self._emit('copy_a:')
        self._emit('  %ia = phi i64 [0, %0], [%ia_next, %copy_a]')
        self._emit('  %ca = getelementptr i8, i8* %a, i64 %ia')
        self._emit('  %va = load i8, i8* %ca')
        self._emit('  %done_a = icmp eq i8 %va, 0')
        self._emit('  br i1 %done_a, label %copy_b, label %copy_a_next')
        self._emit('copy_a_next:')
        self._emit('  %da = getelementptr i8, i8* %buf, i64 %ia')
        self._emit('  store i8 %va, i8* %da')
        self._emit('  %ia_next = add i64 %ia, 1')
        self._emit('  br label %copy_a')
        self._emit('copy_b:')
        self._emit('  %ib = phi i64 [0, %copy_a], [%ib_next, %copy_b]')
        self._emit('  %cb = getelementptr i8, i8* %b, i64 %ib')
        self._emit('  %vb = load i8, i8* %cb')
        self._emit('  %done_b = icmp eq i8 %vb, 0')
        self._emit('  br i1 %done_b, label %done, label %copy_b_next')
        self._emit('copy_b_next:')
        self._emit('  %db = getelementptr i8, i8* %buf, i64 %ia')
        self._emit('  %db2 = getelementptr i8, i8* %db, i64 %ib')
        self._emit('  store i8 %vb, i8* %db2')
        self._emit('  %ib_next = add i64 %ib, 1')
        self._emit('  br label %copy_b')
        self._emit('done:')
        self._emit('  %end = getelementptr i8, i8* %buf, i64 %total')
        self._emit('  store i8 0, i8* %end')
        self._emit('  ret i8* %buf')
        self._emit('}')
        self._emit()

        # Int-to-string helper (for number printing)
        self._emit('; --- Int to String Helper ---')
        self._emit('define i8* @mmc_int_to_str(i64 %n) {')
        self._emit('  %buf = call i8* @malloc(i64 32)')
        self._emit('  %fmt = getelementptr [4 x i8], [4 x i8]* @.int_fmt, i64 0, i64 0')
        self._emit('  call i32 (i8*, ...) @printf(i8* %fmt, i64 %n, i8* %buf)')
        self._emit('  ret i8* %buf')
        self._emit('}')
        self._emit()

        # Float-to-string helper
        self._emit('; --- Float to String Helper ---')
        self._emit('define i8* @mmc_float_to_str(double %n) {')
        self._emit('  %buf = call i8* @malloc(i64 64)')
        self._emit('  %fmt = getelementptr [4 x i8], [4 x i8]* @.float_fmt, i64 0, i64 0')
        self._emit('  call i32 (i8*, ...) @printf(i8* %fmt, double %n, i8* %buf)')
        self._emit('  ret i8* %buf')
        self._emit('}')
        self._emit()

        # String constants
        self._emit('; --- String Constants ---')
        self._emit('  @.int_fmt = private constant [4 x i8] c"%ld\\00"')
        self._emit('  @.float_fmt = private constant [4 x i8] c"%f\\00"')
        self._emit('  @.newline = private constant [2 x i8] c"\\0a\\00"')
        # Printf format strings for print()
        self._emit('  @.str_print_s = private unnamed_addr constant [5 x i8] c"%s\\0a\\00"')
        self._emit('  @.str_print_d = private unnamed_addr constant [5 x i8] c"%ld\\0a\\00"')
        self._emit('  @.str_print_f = private unnamed_addr constant [6 x i8] c"%.6f\\0a\\00"')
        self._emit('  @.str_true = private unnamed_addr constant [6 x i8] c"true\\0a\\00"')
        self._emit('  @.str_false = private unnamed_addr constant [7 x i8] c"false\\0a\\00"')
        self._emit('  @.str_space = private unnamed_addr constant [2 x i8] c" \\00"')
        for sc in self.str_constants:
            self._emit(sc)
        self._emit()

        # Generate user-defined functions (before main)
        for func_name, func_node in self.functions.items():
            if func_name != 'main':
                self._emit_function(func_node)

        # Generate main function
        main_func = self.functions.get('main')
        if main_func:
            self._emit_function_main(main_func)
        else:
            # Wrap program in main()
            self._emit_main_wrapper(ast)

        return '\n'.join(self.output)

    def _emit_function(self, node: ASTNode):
        """Generate a user-defined function."""
        name = node.name
        params = node.value or []  # list of (name, type)
        return_type = node.annotation or 'i64'

        self.current_function = name

        # Function signature
        param_strs = []
        for pname, ptype in params:
            llvm_type = self._map_type(ptype) if ptype else 'i64'
            param_strs.append(f"{llvm_type} %arg_{pname}")

        self._emit(f'define {return_type} @{name}({", ".join(param_strs)}) {{')
        self._emit(f'entry:')

        # Create scope for function
        func_scope = LLVMScope()
        for pname, ptype in params:
            llvm_type = self._map_type(ptype) if ptype else 'i64'
            alloca = self._new_reg()
            self._emit(f'  {alloca} = alloca {llvm_type}')
            self._emit(f'  store {llvm_type} %arg_{pname}, {llvm_type}* {alloca}')
            func_scope.define(pname, llvm_type, alloca)

        old_scope = self.scope
        self.scope = func_scope

        # Generate body
        body = node.children[0] if node.children else None
        if body:
            self._emit_block(body)

        # Default return
        if return_type == 'i64':
            self._emit('  ret i64 0')
        elif return_type == 'double':
            self._emit('  ret double 0.0')
        elif return_type == 'void':
            self._emit('  ret void')
        else:
            self._emit('  ret i64 0')

        self._emit('}')
        self._emit()
        self.scope = old_scope

    def _emit_function_main(self, node: ASTNode):
        """Generate main function from a defined main()."""
        self.current_function = 'main'
        self._emit('define i32 @main() {')
        self._emit('entry:')

        # Init scope
        main_scope = LLVMScope()
        old_scope = self.scope
        self.scope = main_scope

        body = node.children[0] if node.children else None
        if body:
            self._emit_block(body)

        self._emit('  ret i32 0')
        self._emit('}')
        self._emit()
        self.scope = old_scope

    def _emit_main_wrapper(self, ast: ASTNode):
        """Wrap top-level statements in a main() function."""
        self.current_function = 'main'
        self._emit('define i32 @main() {')
        self._emit('entry:')

        main_scope = LLVMScope()
        old_scope = self.scope
        self.scope = main_scope

        # Add the string constants that were generated during preprocessing
        # (already in self.str_constants, emitted above)

        for child in ast.children:
            if child.type != ASTNodeType.FUNC_DEF:
                self._emit_statement(child)

        self._emit('  ret i32 0')
        self._emit('}')
        self._emit()
        self.scope = old_scope

    def _map_type(self, type_name: str) -> str:
        """Map MMC type name to LLVM type."""
        type_map = {
            'int': 'i64', 'ကိန်း': 'i64',
            'float': 'double', 'ပျော်': 'double',
            'str': 'i8*', 'string': 'i8*', 'စာ': 'i8*',
            'bool': 'i1', 'ဟုတ်သွေး': 'i1',
            'void': 'void', 'လိုတယ်': 'void',
            'array': 'i8*', 'စု': 'i8*',
            'dict': 'i8*', 'ထိုင်': 'i8*',
        }
        return type_map.get(type_name, 'i64')

    # --- Statement Generation ---

    def _emit_block(self, node: ASTNode):
        """Generate code for a block of statements."""
        if node.type == ASTNodeType.BLOCK:
            for child in node.children:
                self._emit_statement(child)
        else:
            self._emit_statement(node)

    def _emit_statement(self, node: ASTNode):
        """Generate LLVM IR for a statement."""
        if node.type == ASTNodeType.VAR_DECLARE:
            self._emit_var_decl(node)
        elif node.type == ASTNodeType.ASSIGN:
            self._emit_assign(node)
        elif node.type == ASTNodeType.AUG_ASSIGN:
            self._emit_aug_assign(node)
        elif node.type == ASTNodeType.PRINT_STMT:
            self._emit_print(node)
        elif node.type == ASTNodeType.INPUT_STMT:
            self._emit_input(node)
        elif node.type == ASTNodeType.IF_STMT:
            self._emit_if(node)
        elif node.type == ASTNodeType.WHILE_STMT:
            self._emit_while(node)
        elif node.type in (ASTNodeType.FOR_RANGE, ASTNodeType.FOR_EACH):
            self._emit_for(node)
        elif node.type == ASTNodeType.RETURN_STMT:
            self._emit_return(node)
        elif node.type == ASTNodeType.BREAK_STMT:
            self._emit('  br label %break')
        elif node.type == ASTNodeType.CONTINUE_STMT:
            self._emit('  br label %continue')
        elif node.type == ASTNodeType.PASS_STMT:
            pass  # no-op
        elif node.type == ASTNodeType.ASSERT_STMT:
            self._emit_assert(node)
        elif node.type == ASTNodeType.EXPR_STMT:
            self._gen_expr(node.value)
        elif node.type == ASTNodeType.FUNC_DEF:
            pass  # already processed in first pass
        elif node.type == ASTNodeType.IMPORT_STMT:
            pass  # skip for now
        elif node.type == ASTNodeType.GLOBAL_STMT:
            pass  # skip for now

    def _emit_var_decl(self, node: ASTNode):
        """Generate variable declaration with alloca."""
        name = node.name
        init = node.value

        if init:
            val_reg, val_type = self._gen_expr(init)
            alloca = self._new_reg("%var")
            self._emit(f'  {alloca} = alloca {val_type}')
            self._emit(f'  store {val_type} {val_reg}, {val_type}* {alloca}')
            self.scope.define(name, val_type, alloca)
        else:
            # Declare with default value
            default_type = self._map_type(node.annotation) if node.annotation else 'i64'
            alloca = self._new_reg("%var")
            self._emit(f'  {alloca} = alloca {default_type}')
            if default_type == 'i64':
                self._emit(f'  store i64 0, i64* {alloca}')
            elif default_type == 'double':
                self._emit(f'  store double 0.0, double* {alloca}')
            elif default_type == 'i1':
                self._emit(f'  store i1 false, i1* {alloca}')
            elif default_type == 'i8*':
                null_str = self._get_global_str("")
                self._emit(f'  store i8* {null_str}, i8** {alloca}')
            self.scope.define(name, default_type, alloca)

    def _emit_assign(self, node: ASTNode):
        """Generate assignment: name = expr."""
        name = node.name
        result = self.scope.lookup(name)

        if not result:
            # Auto-declare on first assignment
            val_reg, val_type = self._gen_expr(node.value)
            alloca = self._new_reg("%var")
            self._emit(f'  {alloca} = alloca {val_type}')
            self._emit(f'  store {val_type} {val_reg}, {val_type}* {alloca}')
            self.scope.define(name, val_type, alloca)
            return

        var_type, alloca = result
        val_reg, val_type = self._gen_expr(node.value)

        # Handle type coercion
        if var_type == 'double' and val_type == 'i64':
            conv = self._new_reg()
            self._emit(f'  {conv} = sitofp i64 {val_reg} to double')
            val_reg = conv
            val_type = 'double'
        elif var_type == 'i64' and val_type == 'double':
            conv = self._new_reg()
            self._emit(f'  {conv} = fptosi double {val_reg} to i64')
            val_reg = conv
            val_type = 'i64'

        self._emit(f'  store {val_type} {val_reg}, {val_type}* {alloca}')

    def _emit_aug_assign(self, node: ASTNode):
        """Generate augmented assignment: name += expr."""
        name = node.name
        op = node.annotation  # '+', '-', '*', '/'

        result = self.scope.lookup(name)
        if not result:
            return

        var_type, alloca = result
        old_reg = self._new_reg()
        self._emit(f'  {old_reg} = load {var_type}, {var_type}* {alloca}')

        rhs_reg, rhs_type = self._gen_expr(node.value)

        # Coerce types
        if var_type == 'double' and rhs_type == 'i64':
            conv = self._new_reg()
            self._emit(f'  {conv} = sitofp i64 {rhs_reg} to double')
            rhs_reg = conv
        elif var_type == 'i64' and rhs_type == 'double':
            conv = self._new_reg()
            self._emit(f'  {conv} = fptosi double {rhs_reg} to i64')
            rhs_reg = conv

        result_reg = self._new_reg()

        if var_type == 'double':
            fop = {'+': 'fadd', '-': 'fsub', '*': 'fmul', '/': 'fdiv'}.get(op, 'fadd')
            self._emit(f'  {result_reg} = {fop} double {old_reg}, {rhs_reg}')
        else:
            iop = {'+': 'add', '-': 'sub', '*': 'mul', '/': 'sdiv', '%': 'srem'}.get(op, 'add')
            self._emit(f'  {result_reg} = {iop} {var_type} {old_reg}, {rhs_reg}')

        self._emit(f'  store {var_type} {result_reg}, {var_type}* {alloca}')

    def _emit_print(self, node: ASTNode):
        """Generate print statement with printf."""
        args = node.value if node.value else []
        is_newline = (node.annotation == "newline")

        if not args:
            # Print empty newline
            nl_str = self._get_global_str("\n")
            self._emit(f'  call i32 @puts(i8* {nl_str})')
            return

        for i, arg in enumerate(args):
            val_reg, val_type = self._gen_expr(arg)

            if val_type == 'i8*':
                # Print string directly
                self._emit(f'  call i32 (i8*, ...) @printf(i8* getelementptr ([5 x i8], [5 x i8]* @.str_print_s, i64 0, i64 0), i8* {val_reg})')
            elif val_type == 'double':
                self._emit(f'  call i32 (i8*, ...) @printf(i8* getelementptr ([5 x i8], [5 x i8]* @.str_print_f, i64 0, i64 0), double {val_reg})')
            elif val_type == 'i1':
                # Print true/false
                true_lbl = self._new_label()
                false_lbl = self._new_label()
                end_lbl = self._new_label()
                self._emit(f'  br i1 {val_reg}, label %{true_lbl}, label %{false_lbl}')
                self._emit(f'{true_lbl}:')
                self._emit(f'  call i32 @puts(i8* getelementptr ([6 x i8], [6 x i8]* @.str_true, i64 0, i64 0))')
                self._emit(f'  br label %{end_lbl}')
                self._emit(f'{false_lbl}:')
                self._emit(f'  call i32 @puts(i8* getelementptr ([7 x i8], [7 x i8]* @.str_false, i64 0, i64 0))')
                self._emit(f'  br label %{end_lbl}')
                self._emit(f'{end_lbl}:')
            else:
                # Print integer
                self._emit(f'  call i32 (i8*, ...) @printf(i8* getelementptr ([5 x i8], [5 x i8]* @.str_print_d, i64 0, i64 0), i64 {val_reg})')

            # Print space between args (except last)
            if i < len(args) - 1:
                self._emit(f'  call i32 @puts(i8* getelementptr ([2 x i8], [2 x i8]* @.str_space, i64 0, i64 0))')

        if is_newline:
            self._emit(f'  call i32 @puts(i8* getelementptr ([2 x i8], [2 x i8]* @.newline, i64 0, i64 0))')

    def _emit_input(self, node: ASTNode):
        """Generate input statement (simplified - reads a line)."""
        # For LLVM IR, we just allocate a buffer and call scanf/gets
        self._emit('  ; TODO: input() - not yet implemented for LLVM backend')

    def _emit_if(self, node: ASTNode):
        """Generate if/elif/else chain."""
        else_label = self._new_label("if_end")
        self._emit_if_chain(node, else_label)
        self._emit(f'{else_label}:')

    def _emit_if_chain(self, node: ASTNode, end_label: str):
        """Recursive if/elif/else generation."""
        cond_node = node.value
        then_block = node.children[0] if node.children else None

        if cond_node:
            # Has condition (if or elif)
            cond_reg, cond_type = self._gen_expr(cond_node)
            # Convert to i1 if needed
            if cond_type == 'i64':
                cmp_reg = self._new_reg()
                self._emit(f'  {cmp_reg} = icmp ne i64 {cond_reg}, 0')
                cond_reg = cmp_reg

            # Create labels
            then_end = self._new_label("then")
            has_else = len(node.children) > 1

            if has_else:
                # Has else/elif branches
                else_label = self._new_label("else")
                self._emit(f'  br i1 {cond_reg}, label %{then_end}, label %{else_label}')
            else:
                # If only (no else)
                self._emit(f'  br i1 {cond_reg}, label %{then_end}, label %{end_label}')

            # Then block
            self._emit(f'{then_end}:')
            if then_block:
                self._emit_block(then_block)
            self._emit(f'  br label %{end_label}')

            if has_else:
                # Else/elif chain
                self._emit(f'{else_label}:')
                else_node = node.children[1]
                if else_node.type == ASTNodeType.IF_STMT and else_node.value:
                    # elif
                    self._emit_if_chain(else_node, end_label)
                else:
                    # else
                    else_body = else_node.children[0] if else_node.children else None
                    if else_body:
                        self._emit_block(else_body)
                    self._emit(f'  br label %{end_label}')
        else:
            # Else block (no condition)
            if then_block:
                self._emit_block(then_block)
            self._emit(f'  br label %{end_label}')

    def _emit_while(self, node: ASTNode):
        """Generate while loop."""
        cond_label = self._new_label("while_cond")
        body_label = self._new_label("while_body")
        end_label = self._new_label("while_end")

        # Use these labels for break/continue
        old_break = getattr(self, '_break_label', None)
        old_continue = getattr(self, '_continue_label', None)
        self._break_label = end_label
        self._continue_label = cond_label

        self._emit(f'  br label %{cond_label}')

        # Condition check
        self._emit(f'{cond_label}:')
        cond_reg, cond_type = self._gen_expr(node.value)
        if cond_type == 'i64':
            cmp_reg = self._new_reg()
            self._emit(f'  {cmp_reg} = icmp ne i64 {cond_reg}, 0')
            cond_reg = cmp_reg
        self._emit(f'  br i1 {cond_reg}, label %{body_label}, label %{end_label}')

        # Loop body
        self._emit(f'{body_label}:')
        body = node.children[0] if node.children else None
        if body:
            self._emit_block(body)
        self._emit(f'  br label %{cond_label}')

        # End
        self._emit(f'{end_label}:')

        # Restore break/continue labels
        self._break_label = old_break
        self._continue_label = old_continue

    def _emit_for(self, node: ASTNode):
        """Generate for loop (range or foreach)."""
        if node.type == ASTNodeType.FOR_RANGE:
            self._emit_for_range(node)
        else:
            # ForEach - simplified (just basic array iteration)
            self._emit_for_range(node)

    def _emit_for_range(self, node: ASTNode):
        """Generate for-range loop: for i in range(start, stop, step)"""
        var_name = node.name
        iterable = node.value  # This is the range() call

        # Extract range arguments
        args = iterable.value if iterable.type == ASTNodeType.CALL and iterable.value else []

        # Default: range(stop) => range(0, stop, 1)
        start_val = 0
        step_val = 1

        if len(args) == 1:
            stop_reg, _ = self._gen_expr(args[0])
        elif len(args) >= 2:
            start_reg, _ = self._gen_expr(args[0])
            start_val = None  # use register
            stop_reg, _ = self._gen_expr(args[1])
        elif len(args) >= 3:
            start_reg, _ = self._gen_expr(args[0])
            start_val = None
            stop_reg, _ = self._gen_expr(args[1])
            step_reg, _ = self._gen_expr(args[2])
            step_val = None
        else:
            return

        # Create loop variable
        alloca = self._new_reg("%loop_var")
        self._emit(f'  {alloca} = alloca i64')

        # Initialize loop variable
        if start_val is not None:
            self._emit(f'  store i64 {start_val}, i64* {alloca}')
            start_reg = str(start_val)
        else:
            self._emit(f'  store i64 {start_reg}, i64* {alloca}')

        self.scope.define(var_name, 'i64', alloca)

        cond_label = self._new_label("for_cond")
        body_label = self._new_label("for_body")
        end_label = self._new_label("for_end")

        old_break = getattr(self, '_break_label', None)
        old_continue = getattr(self, '_continue_label', None)
        self._break_label = end_label
        self._continue_label = cond_label

        self._emit(f'  br label %{cond_label}')

        # Condition: i < stop
        self._emit(f'{cond_label}:')
        cur_reg = self._new_reg()
        self._emit(f'  {cur_reg} = load i64, i64* {alloca}')
        cmp_reg = self._new_reg()
        self._emit(f'  {cmp_reg} = icmp slt i64 {cur_reg}, {stop_reg}')
        self._emit(f'  br i1 {cmp_reg}, label %{body_label}, label %{end_label}')

        # Body
        self._emit(f'{body_label}:')
        body = node.children[0] if node.children else None
        if body:
            self._emit_block(body)

        # Increment
        inc_reg = self._new_reg()
        if step_val is not None:
            self._emit(f'  {inc_reg} = add i64 {cur_reg}, {step_val}')
        else:
            step_load = self._new_reg()
            self._emit(f'  {step_load} = add i64 {cur_reg}, {step_reg}')
            inc_reg = step_load
        self._emit(f'  store i64 {inc_reg}, i64* {alloca}')

        self._emit(f'  br label %{cond_label}')
        self._emit(f'{end_label}:')

        self._break_label = old_break
        self._continue_label = old_continue

    def _emit_return(self, node: ASTNode):
        """Generate return statement."""
        if node.value:
            val_reg, val_type = self._gen_expr(node.value)
            if val_type == 'double':
                self._emit(f'  ret double {val_reg}')
            elif val_type == 'i1':
                ext = self._new_reg()
                self._emit(f'  {ext} = zext i1 {val_reg} to i32')
                self._emit(f'  ret i32 {ext}')
            else:
                self._emit(f'  ret i64 {val_reg}')
        else:
            self._emit('  ret i64 0')

    def _emit_assert(self, node: ASTNode):
        """Generate assert statement."""
        cond_reg, cond_type = self._gen_expr(node.value)
        if cond_type == 'i64':
            cmp = self._new_reg()
            self._emit(f'  {cmp} = icmp ne i64 {cond_reg}, 0')
            cond_reg = cmp

        fail_label = self._new_label("assert_fail")
        ok_label = self._new_label("assert_ok")
        self._emit(f'  br i1 {cond_reg}, label %{ok_label}, label %{fail_label}')
        self._emit(f'{fail_label}:')
        msg = node.children[0] if node.children else None
        if msg and msg.type == ASTNodeType.STRING_LIT:
            err_str = self._get_global_str(msg.value)
            self._emit(f'  call i32 @puts(i8* {err_str})')
        self._emit(f'  call void @exit(i32 1)')
        self._emit(f'{ok_label}:')

    # --- Expression Generation ---

    def _gen_expr(self, node: ASTNode) -> Tuple[str, str]:
        """Generate expression, returns (register, llvm_type)."""
        if node.type == ASTNodeType.INT_LIT:
            return str(node.value), 'i64'
        elif node.type == ASTNodeType.FLOAT_LIT:
            return str(node.value), 'double'
        elif node.type == ASTNodeType.STRING_LIT:
            str_reg = self._get_global_str(node.value)
            return str_reg, 'i8*'
        elif node.type == ASTNodeType.BOOL_LIT:
            return '1' if node.value else '0', 'i1'
        elif node.type == ASTNodeType.NONE_LIT:
            return '0', 'i64'
        elif node.type == ASTNodeType.IDENT:
            return self._gen_ident(node)
        elif node.type == ASTNodeType.BINOP:
            return self._gen_binop(node)
        elif node.type == ASTNodeType.UNOP:
            return self._gen_unop(node)
        elif node.type == ASTNodeType.CALL:
            return self._gen_call(node)
        elif node.type == ASTNodeType.ARRAY_LIT:
            return self._gen_array_lit(node)
        elif node.type == ASTNodeType.DICT_LIT:
            return '0', 'i64'
        elif node.type == ASTNodeType.MEMBER_ACCESS:
            return '0', 'i64'
        elif node.type == ASTNodeType.INDEX_ACCESS:
            return '0', 'i64'
        elif node.type == ASTNodeType.TERNARY:
            return self._gen_ternary(node)
        else:
            return '0', 'i64'

    def _gen_ident(self, node: ASTNode) -> Tuple[str, str]:
        """Generate identifier reference (load from alloca)."""
        result = self.scope.lookup(node.name)
        if result:
            var_type, alloca = result
            reg = self._new_reg()
            self._emit(f'  {reg} = load {var_type}, {var_type}* {alloca}')
            return reg, var_type

        # Unknown variable - return 0
        return '0', 'i64'

    def _gen_binop(self, node: ASTNode) -> Tuple[str, str]:
        """Generate binary operation."""
        op = node.value

        # Short-circuit for && and ||
        if op == '&&':
            return self._gen_logic_and(node)
        elif op == '||':
            return self._gen_logic_or(node)

        left_reg, left_type = self._gen_expr(node.children[0])
        right_reg, right_type = self._gen_expr(node.children[1])

        # Promote to double if either is float
        if left_type == 'double' or right_type == 'double':
            if left_type == 'i64':
                conv = self._new_reg()
                self._emit(f'  {conv} = sitofp i64 {left_reg} to double')
                left_reg, left_type = conv, 'double'
            if right_type == 'i64':
                conv = self._new_reg()
                self._emit(f'  {conv} = sitofp i64 {right_reg} to double')
                right_reg, right_type = conv, 'double'

        result = self._new_reg()

        if left_type == 'double':
            ops = {
                '+': 'fadd', '-': 'fsub', '*': 'fmul',
                '/': 'fdiv', '%': 'frem',
            }
            cmp_ops = {
                '==': 'oeq', '!=': 'one', '<': 'olt',
                '>': 'ogt', '<=': 'ole', '>=': 'oge',
            }
            if op in ops:
                self._emit(f'  {result} = {ops[op]} double {left_reg}, {right_reg}')
                return result, 'double'
            elif op in cmp_ops:
                cmp = self._new_reg()
                self._emit(f'  {cmp} = fcmp {cmp_ops[op]} double {left_reg}, {right_reg}')
                ext = self._new_reg()
                self._emit(f'  {ext} = zext i1 {cmp} to i64')
                return ext, 'i64'
            elif op == '**':
                # Power - use __powidf2 or manual
                self._emit(f'  {result} = call double @pow(double {left_reg}, double {right_reg})')
                return result, 'double'
        else:
            # Integer operations
            ops = {
                '+': 'add', '-': 'sub', '*': 'mul',
                '/': 'sdiv', '%': 'srem',
            }
            cmp_ops = {
                '==': 'eq', '!=': 'ne', '<': 'slt',
                '>': 'sgt', '<=': 'sle', '>=': 'sge',
            }
            if op in ops:
                self._emit(f'  {result} = {ops[op]} i64 {left_reg}, {right_reg}')
                return result, 'i64'
            elif op in cmp_ops:
                cmp = self._new_reg()
                self._emit(f'  {cmp} = icmp {cmp_ops[op]} i64 {left_reg}, {right_reg}')
                ext = self._new_reg()
                self._emit(f'  {ext} = zext i1 {cmp} to i64')
                return ext, 'i64'
            elif op == '**':
                # Integer power
                self._emit(f'  ; TODO: integer power not implemented')
                return left_reg, 'i64'

        # String concatenation
        if op == '+' and left_type == 'i8*' and right_type == 'i8*':
            cat = self._new_reg()
            self._emit(f'  {cat} = call i8* @mmc_strcat(i8* {left_reg}, i8* {right_reg})')
            return cat, 'i8*'

        return result, 'i64'

    def _gen_logic_and(self, node: ASTNode) -> Tuple[str, str]:
        """Generate short-circuit AND."""
        lhs_reg, lhs_type = self._gen_expr(node.children[0])
        if lhs_type == 'i64':
            cmp = self._new_reg()
            self._emit(f'  {cmp} = icmp ne i64 {lhs_reg}, 0')
            lhs_reg = cmp
            lhs_type = 'i1'

        rhs_label = self._new_label("and_rhs")
        end_label = self._new_label("and_end")

        result = self._new_reg()
        self._emit(f'  br i1 {lhs_reg}, label %{rhs_label}, label %{end_label}')
        self._emit(f'{rhs_label}:')
        rhs_reg, rhs_type = self._gen_expr(node.children[1])
        if rhs_type == 'i64':
            cmp2 = self._new_reg()
            self._emit(f'  {cmp2} = icmp ne i64 {rhs_reg}, 0')
            rhs_reg = cmp2
        self._emit(f'  br label %{end_label}')
        self._emit(f'{end_label}:')
        phi = self._new_reg()
        self._emit(f'  {phi} = phi i1 [false, %0], [{rhs_reg}, %{rhs_label}]')
        ext = self._new_reg()
        self._emit(f'  {ext} = zext i1 {phi} to i64')
        return ext, 'i64'

    def _gen_logic_or(self, node: ASTNode) -> Tuple[str, str]:
        """Generate short-circuit OR."""
        lhs_reg, lhs_type = self._gen_expr(node.children[0])
        if lhs_type == 'i64':
            cmp = self._new_reg()
            self._emit(f'  {cmp} = icmp ne i64 {lhs_reg}, 0')
            lhs_reg = cmp
            lhs_type = 'i1'

        rhs_label = self._new_label("or_rhs")
        end_label = self._new_label("or_end")

        self._emit(f'  br i1 {lhs_reg}, label %{end_label}, label %{rhs_label}')
        self._emit(f'{rhs_label}:')
        rhs_reg, rhs_type = self._gen_expr(node.children[1])
        if rhs_type == 'i64':
            cmp2 = self._new_reg()
            self._emit(f'  {cmp2} = icmp ne i64 {rhs_reg}, 0')
            rhs_reg = cmp2
        self._emit(f'  br label %{end_label}')
        self._emit(f'{end_label}:')
        phi = self._new_reg()
        self._emit(f'  {phi} = phi i1 [true, %0], [{rhs_reg}, %{rhs_label}]')
        ext = self._new_reg()
        self._emit(f'  {ext} = zext i1 {phi} to i64')
        return ext, 'i64'

    def _gen_unop(self, node: ASTNode) -> Tuple[str, str]:
        """Generate unary operation."""
        op = node.value
        operand_reg, operand_type = self._gen_expr(node.children[0])

        if op == '-':
            result = self._new_reg()
            if operand_type == 'double':
                self._emit(f'  {result} = fneg double {operand_reg}')
            else:
                self._emit(f'  {result} = sub i64 0, {operand_reg}')
            return result, operand_type
        elif op == '!':
            if operand_type == 'i64':
                cmp = self._new_reg()
                self._emit(f'  {cmp} = icmp eq i64 {operand_reg}, 0')
                ext = self._new_reg()
                self._emit(f'  {ext} = zext i1 {cmp} to i64')
                return ext, 'i64'
            else:
                inv = self._new_reg()
                self._emit(f'  {inv} = xor i1 {operand_reg}, true')
                ext = self._new_reg()
                self._emit(f'  {ext} = zext i1 {inv} to i64')
                return ext, 'i64'
        elif op in ('++', '--'):
            # Pre-increment/decrement
            result = self._new_reg()
            if op == '++':
                self._emit(f'  {result} = add i64 {operand_reg}, 1')
            else:
                self._emit(f'  {result} = sub i64 {operand_reg}, 1')

            # If the operand is an identifier, store back
            ident = node.children[0]
            if ident.type == ASTNodeType.IDENT:
                lookup = self.scope.lookup(ident.name)
                if lookup:
                    var_type, alloca = lookup
                    self._emit(f'  store i64 {result}, i64* {alloca}')
            return result, 'i64'

        return operand_reg, operand_type

    def _gen_call(self, node: ASTNode) -> Tuple[str, str]:
        """Generate function call."""
        func_name = node.name
        args = node.value or []

        # Built-in functions
        if func_name == 'print':
            self._emit_print(ASTNode(ASTNodeType.PRINT_STMT, value=args))
            return '0', 'i64'
        elif func_name == 'println':
            self._emit_print(ASTNode(ASTNodeType.PRINT_STMT, value=args, annotation="newline"))
            return '0', 'i64'
        elif func_name == 'len':
            # Get length of something
            if args:
                arg_reg, arg_type = self._gen_expr(args[0])
                if arg_type == 'i8*':
                    len_reg = self._new_reg()
                    self._emit(f'  {len_reg} = call i64 @strlen(i8* {arg_reg})')
                    return len_reg, 'i64'
            return '0', 'i64'
        elif func_name == 'abs':
            if args:
                arg_reg, arg_type = self._gen_expr(args[0])
                result = self._new_reg()
                if arg_type == 'double':
                    self._emit(f'  {result} = call double @fabs(double {arg_reg})')
                    return result, 'double'
                else:
                    neg = self._new_reg()
                    self._emit(f'  {neg} = sub i64 0, {arg_reg}')
                    cmp = self._new_reg()
                    self._emit(f'  {cmp} = icmp slt i64 {arg_reg}, 0')
                    sel = self._new_reg()
                    self._emit(f'  {sel} = select i1 {cmp}, i64 {neg}, i64 {arg_reg}')
                    return sel, 'i64'
            return '0', 'i64'
        elif func_name == 'range':
            # range() returns a special marker, handled in for loop
            return '0', 'i64'
        elif func_name == 'type':
            if args:
                _, arg_type = self._gen_expr(args[0])
                type_names = {'i64': 'int', 'double': 'float', 'i8*': 'str', 'i1': 'bool'}
                type_str = self._get_global_str(type_names.get(arg_type, 'unknown'))
                return type_str, 'i8*'
            return '0', 'i64'
        elif func_name == 'str':
            # Convert to string
            if args:
                arg_reg, arg_type = self._gen_expr(args[0])
                if arg_type == 'i64':
                    result = self._new_reg()
                    self._emit(f'  {result} = call i8* @mmc_int_to_str(i64 {arg_reg})')
                    return result, 'i8*'
                elif arg_type == 'double':
                    result = self._new_reg()
                    self._emit(f'  {result} = call i8* @mmc_float_to_str(double {arg_reg})')
                    return result, 'i8*'
                return arg_reg, arg_type
            empty = self._get_global_str("")
            return empty, 'i8*'
        elif func_name == 'int':
            if args:
                arg_reg, arg_type = self._gen_expr(args[0])
                if arg_type == 'double':
                    result = self._new_reg()
                    self._emit(f'  {result} = fptosi double {arg_reg} to i64')
                    return result, 'i64'
                return arg_reg, 'i64'
            return '0', 'i64'
        elif func_name == 'float':
            if args:
                arg_reg, arg_type = self._gen_expr(args[0])
                if arg_type == 'i64':
                    result = self._new_reg()
                    self._emit(f'  {result} = sitofp i64 {arg_reg} to double')
                    return result, 'double'
                return arg_reg, arg_type
            return '0.0', 'double'

        # User-defined function call
        arg_strs = []
        arg_types = []
        for arg in args:
            reg, atype = self._gen_expr(arg)
            arg_strs.append(f"{atype} {reg}")
            arg_types.append(atype)

        ret_type = 'i64'
        func_def = self.functions.get(func_name)
        if func_def:
            ret_type = self._map_type(func_def.annotation) if func_def.annotation else 'i64'

        result = self._new_reg()
        self._emit(f'  {result} = call {ret_type} @{func_name}({", ".join(arg_strs)})')
        return result, ret_type

    def _gen_array_lit(self, node: ASTNode) -> Tuple[str, str]:
        """Generate array literal (simplified - stores as null ptr)."""
        return 'null', 'i8*'

    def _gen_ternary(self, node: ASTNode) -> Tuple[str, str]:
        """Generate ternary expression: cond ? then : else"""
        cond_reg, _ = self._gen_expr(node.children[0])
        if _ == 'i64':
            cmp = self._new_reg()
            self._emit(f'  {cmp} = icmp ne i64 {cond_reg}, 0')
            cond_reg = cmp

        then_label = self._new_label("tern_then")
        else_label = self._new_label("tern_else")
        end_label = self._new_label("tern_end")

        self._emit(f'  br i1 {cond_reg}, label %{then_label}, label %{else_label}')

        self._emit(f'{then_label}:')
        then_reg, then_type = self._gen_expr(node.children[1])
        then_end = self._new_reg("tmp")
        if then_type == 'double':
            self._emit(f'  {then_end} = fptrunc double {then_reg} to double ; no-op placeholder')
        else:
            self._emit(f'  {then_end} = add i64 {then_reg}, 0 ; no-op to get SSA value')
        self._emit(f'  br label %{end_label}')

        self._emit(f'{else_label}:')
        else_reg, else_type = self._gen_expr(node.children[2])
        else_end = self._new_reg("tmp")
        if else_type == 'double':
            self._emit(f'  {else_end} = fptrunc double {else_reg} to double')
        else:
            self._emit(f'  {else_end} = add i64 {else_reg}, 0')
        self._emit(f'  br label %{end_label}')

        self._emit(f'{end_label}:')
        phi = self._new_reg()
        # Use i64 as common type for phi
        self._emit(f'  {phi} = phi i64 [{then_end}, %{then_label}], [{else_end}, %{else_label}]')

        return phi, 'i64'


def compile_to_llvm(source: str, filename: str = "<mmc>") -> str:
    """Compile MMC source to LLVM IR string."""
    tokens = tokenize(source, filename)
    ast = parse(tokens)
    codegen = MMCCodegenLLVM()

    # We need to pre-scan for all strings used in the program
    # to ensure global string constants are emitted
    _pre_scan_strings(codegen, ast)

    ir = codegen.generate(ast)
    return ir


def _pre_scan_strings(codegen: MMCCodegenLLVM, node: ASTNode):
    """Pre-scan AST for all string literals to ensure globals are created."""
    if node.type == ASTNodeType.STRING_LIT and isinstance(node.value, str):
        codegen._get_global_str(node.value)
    for child in node.children:
        _pre_scan_strings(codegen, child)
    if node.value and isinstance(node.value, list):
        for item in node.value:
            if isinstance(item, ASTNode):
                _pre_scan_strings(codegen, item)
            elif isinstance(item, tuple):
                for t_item in item:
                    if isinstance(t_item, ASTNode):
                        _pre_scan_strings(codegen, t_item)
    if node.value and isinstance(node.value, ASTNode):
        _pre_scan_strings(codegen, node.value)


def compile_file(input_path: str, output_path: str = None):
    """Compile an MMC file to LLVM IR."""
    with open(input_path, 'r', encoding='utf-8') as f:
        source = f.read()

    ir = compile_to_llvm(source, input_path)

    if output_path is None:
        output_path = input_path.rsplit('.', 1)[0] + '.ll'

    with open(output_path, 'w', encoding='utf-8') as f:
        f.write(ir)

    return output_path


if __name__ == "__main__":
    import sys

    if len(sys.argv) < 2:
        print("MMC LLVM Codegen v2.0.0")
        print("Usage: python3 mmc_codegen_llvm.py <input.mmc> [output.ll]")
        sys.exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2] if len(sys.argv) > 2 else None

    result = compile_file(input_file, output_file)
    print(f"Generated: {result}")
