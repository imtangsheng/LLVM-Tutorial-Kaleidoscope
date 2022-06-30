#!/bin/bash

# function test() {
#     ./test
# }

make clean
make -s test

./test
# test

echo "All tests passed"

# ===----------------------------------------------------------------------===//
# Chapter #1: Kaleidoscope language and Lexer
# Chapter #2: Implementing a Parser and AST
# ===----------------------------------------------------------------------===//

# $ ./test
# ready> def foo(x y) x+foo(y, 4.0);
# Parsed a function definition.
# ready> def foo(x y) x+y y;
# Parsed a function definition.
# Parsed a top-level expr
# ready> def foo(x y) x+y );
# Parsed a function definition.
# Error: unknown token when expecting an expression
# ready> extern sin(a);
# ready> Parsed an extern
# ready> ^D
# $



# ===----------------------------------------------------------------------===//
# Chapter #3: Code generation to LLVM IR
# ===----------------------------------------------------------------------===//

# $ ./test
# ready> 4+5;
# Read top-level expression:
# define double @0() {
# entry:
#   ret double 9.000000e+00
# }

# ready> def foo(a b) a*a + 2*a*b + b*b;
# Read function definition:
# define double @foo(double %a, double %b) {
# entry:
#   %multmp = fmul double %a, %a
#   %multmp1 = fmul double 2.000000e+00, %a
#   %multmp2 = fmul double %multmp1, %b
#   %addtmp = fadd double %multmp, %multmp2
#   %multmp3 = fmul double %b, %b
#   %addtmp4 = fadd double %addtmp, %multmp3
#   ret double %addtmp4
# }

# ready> def bar(a) foo(a, 4.0) + bar(31337);
# Read function definition:
# define double @bar(double %a) {
# entry:
#   %calltmp = call double @foo(double %a, double 4.000000e+00)
#   %calltmp1 = call double @bar(double 3.133700e+04)
#   %addtmp = fadd double %calltmp, %calltmp1
#   ret double %addtmp
# }

# ready> extern cos(x);
# Read extern:
# declare double @cos(double)

# ready> cos(1.234);
# Read top-level expression:
# define double @1() {
# entry:
#   %calltmp = call double @cos(double 1.234000e+00)
#   ret double %calltmp
# }

# ready> ^D
# ; ModuleID = 'my cool jit'

# define double @0() {
# entry:
#   %addtmp = fadd double 4.000000e+00, 5.000000e+00
#   ret double %addtmp
# }

# define double @foo(double %a, double %b) {
# entry:
#   %multmp = fmul double %a, %a
#   %multmp1 = fmul double 2.000000e+00, %a
#   %multmp2 = fmul double %multmp1, %b
#   %addtmp = fadd double %multmp, %multmp2
#   %multmp3 = fmul double %b, %b
#   %addtmp4 = fadd double %addtmp, %multmp3
#   ret double %addtmp4
# }

# define double @bar(double %a) {
# entry:
#   %calltmp = call double @foo(double %a, double 4.000000e+00)
#   %calltmp1 = call double @bar(double 3.133700e+04)
#   %addtmp = fadd double %calltmp, %calltmp1
#   ret double %addtmp
# }

# declare double @cos(double)

# define double @1() {
# entry:
#   %calltmp = call double @cos(double 1.234000e+00)
#   ret double %calltmp
# }


# ===----------------------------------------------------------------------===//
# Chapter #4: Adding JIT and Optimizer Support 
# ===----------------------------------------------------------------------===//
