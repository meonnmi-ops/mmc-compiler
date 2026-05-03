; ======================================================
; MMC Compiler - LLVM IR Output (Test with printf)
; ======================================================
; This file tests the full LLVM IR -> llc -> gcc -> run
; pipeline on ARM64 (Termux / Android)
; ======================================================

target triple = "aarch64-unknown-linux-android"
target datalayout = "e-m:e-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128"

; --- External Declarations ---
declare i32 @printf(i8*, ...)

; --- String Constants ---
@.str.1 = private unnamed_addr constant [20 x i8] c"Hello from MMC v8.2!\0a\00"
@.str.2 = private unnamed_addr constant [14 x i8] c"Fibonacci(10) \00"
@.str.3 = private unnamed_addr constant [7 x i8] c" = %ld\0a\00"
@.str.4 = private unnamed_addr constant [26 x i8] c"Testing arithmetic: %ld\0a\00"
@.str.5 = private unnamed_addr constant [5 x i8] c"%ld\0a\00"
@.str.6 = private unnamed_addr constant [2 x i8] c"\0a\00"
@.str.7 = private unnamed_addr constant [30 x i8] c"Pipeline working correctly!\0a\00"
@.str.8 = private unnamed_addr constant [35 x i8] c"MMC LLVM Backend is LIVE on Termux\0a\00"
@.str.9 = private unnamed_addr constant [5 x i8] c"%s\0a\00"

; --- mmc_fibonacci function ---
define i64 @mmc_fibonacci(i64 %n) {
entry:
    %cmp0 = icmp sle i64 %n, 0
    br i1 %cmp0, label %ret_zero, label %check1

ret_zero:
    ret i64 0

check1:
    %cmp1 = icmp eq i64 %n, 1
    br i1 %cmp1, label %ret_one, label %recurse

ret_one:
    ret i64 1

recurse:
    %n1 = sub i64 %n, 1
    %n2 = sub i64 %n, 2
    %f1 = call i64 @mmc_fibonacci(i64 %n1)
    %f2 = call i64 @mmc_fibonacci(i64 %n2)
    %result = add i64 %f1, %f2
    ret i64 %result
}

; --- mmc_factorial function ---
define i64 @mmc_factorial(i64 %n) {
entry:
    %cmp = icmp sle i64 %n, 1
    br i1 %cmp, label %base, label %recurse

base:
    ret i64 1

recurse:
    %n1 = sub i64 %n, 1
    %fact = call i64 @mmc_factorial(i64 %n1)
    %result = mul i64 %n, %fact
    ret i64 %result
}

; --- mmc_count function (sum 1..n) ---
define i64 @mmc_count(i64 %n) {
entry:
    %i.alloca = alloca i64
    %sum.alloca = alloca i64
    store i64 0, i64* %sum.alloca
    store i64 1, i64* %i.alloca
    br label %loop_cond

loop_cond:
    %i = load i64, i64* %i.alloca
    %cmp = icmp sle i64 %i, %n
    br i1 %cmp, label %loop_body, label %loop_end

loop_body:
    %sum = load i64, i64* %sum.alloca
    %new_sum = add i64 %sum, %i
    store i64 %new_sum, i64* %sum.alloca
    %i.next = add i64 %i, 1
    store i64 %i.next, i64* %i.alloca
    br label %loop_cond

loop_end:
    %result = load i64, i64* %sum.alloca
    ret i64 %result
}

; --- Main Function ---
define i32 @main() {
entry:
    ; === Header ===
    call i32 (i8*, ...) @printf(i8* getelementptr ([35 x i8], [35 x i8]* @.str.8, i64 0, i64 0))

    ; === Test 1: String output ===
    call i32 (i8*, ...) @printf(i8* getelementptr ([20 x i8], [20 x i8]* @.str.1, i64 0, i64 0))

    ; === Test 2: Integer output ===
    call i32 (i8*, ...) @printf(i8* getelementptr ([26 x i8], [26 x i8]* @.str.4, i64 0, i64 0), i64 42)

    ; === Test 3: Arithmetic ===
    %a = add i64 100, 200
    call i32 (i8*, ...) @printf(i8* getelementptr ([5 x i8], [5 x i8]* @.str.5, i64 0, i64 0), i64 %a)

    %b = mul i64 6, 7
    call i32 (i8*, ...) @printf(i8* getelementptr ([5 x i8], [5 x i8]* @.str.5, i64 0, i64 0), i64 %b)

    %c = sdiv i64 100, 3
    call i32 (i8*, ...) @printf(i8* getelementptr ([5 x i8], [5 x i8]* @.str.5, i64 0, i64 0), i64 %c)

    ; Newline
    call i32 (i8*, ...) @printf(i8* getelementptr ([2 x i8], [2 x i8]* @.str.6, i64 0, i64 0))

    ; === Test 4: Fibonacci ===
    %fib10 = call i64 @mmc_fibonacci(i64 10)
    call i32 (i8*, ...) @printf(i8* getelementptr ([14 x i8], [14 x i8]* @.str.2, i64 0, i64 0))
    call i32 (i8*, ...) @printf(i8* getelementptr ([7 x i8], [7 x i8]* @.str.3, i64 0, i64 0), i64 %fib10)

    %fib20 = call i64 @mmc_fibonacci(i64 20)
    call i32 (i8*, ...) @printf(i8* getelementptr ([14 x i8], [14 x i8]* @.str.2, i64 0, i64 0))
    call i32 (i8*, ...) @printf(i8* getelementptr ([7 x i8], [7 x i8]* @.str.3, i64 0, i64 0), i64 %fib20)

    ; === Test 5: Factorial ===
    %fact5 = call i64 @mmc_factorial(i64 5)
    call i32 (i8*, ...) @printf(i8* getelementptr ([5 x i8], [5 x i8]* @.str.5, i64 0, i64 0), i64 %fact5)

    %fact10 = call i64 @mmc_factorial(i64 10)
    call i32 (i8*, ...) @printf(i8* getelementptr ([5 x i8], [5 x i8]* @.str.5, i64 0, i64 0), i64 %fact10)

    ; === Test 6: Sum (loop test) ===
    %sum100 = call i64 @mmc_count(i64 100)
    call i32 (i8*, ...) @printf(i8* getelementptr ([5 x i8], [5 x i8]* @.str.5, i64 0, i64 0), i64 %sum100)

    ; === Final message ===
    call i32 (i8*, ...) @printf(i8* getelementptr ([2 x i8], [2 x i8]* @.str.6, i64 0, i64 0))
    call i32 (i8*, ...) @printf(i8* getelementptr ([30 x i8], [30 x i8]* @.str.7, i64 0, i64 0))

    ret i32 0
}
