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

# ready> extern sin(x);
# Read extern:
# declare double @sin(double)

# ready> extern cos(x);
# Read extern:
# declare double @cos(double)

# ready> sin(1.0);
# Read top-level expression:
# define double @2() {
# entry:
#   ret double 0x3FEAED548F090CEE
# }

# Evaluated to 0.841471

# ready> def foo(x) sin(x)*sin(x) + cos(x)*cos(x);
# Read function definition:
# define double @foo(double %x) {
# entry:
#   %calltmp = call double @sin(double %x)
#   %multmp = fmul double %calltmp, %calltmp
#   %calltmp2 = call double @cos(double %x)
#   %multmp4 = fmul double %calltmp2, %calltmp2
#   %addtmp = fadd double %multmp, %multmp4
#   ret double %addtmp
# }

# ready> foo(4.0);
# Read top-level expression:
# define double @3() {
# entry:
#   %calltmp = call double @foo(double 4.000000e+00)
#   ret double %calltmp
# }

# Evaluated to 1.000000

# ===----------------------------------------------------------------------===//
# 5. Kaleidoscope: Extending the Language: Control Flow 
# ===----------------------------------------------------------------------===//

# extern foo();
# extern bar();
# def baz(x) if x then foo() else bar();

# extern putchard(char);
# def printstar(n) for i = 1, i < n, 1.0 in putchard(42);

# ascii 42 = '*'

# # print 100 '*' characters
# printstar(100);


# ===----------------------------------------------------------------------===//
# 6. Kaleidoscope: Extending the Language: User-defined Operators
# ===----------------------------------------------------------------------===//

    # ready> extern printd(x);
    # Read extern:
    # declare double @printd(double)

    # ready> def binary : 1 (x y) 0;  # Low-precedence operator that ignores operands.
    # ...
    # ready> printd(123) : printd(456) : printd(789);
    # 123.000000
    # 456.000000
    # 789.000000
    # Evaluated to 0.000000

    # ready> extern putchard(char);
    # ...
    # ready> def printdensity(d)
    # if d > 8 then
    #     putchard(32)  # ' '
    # else if d > 4 then
    #     putchard(46)  # '.'
    # else if d > 2 then
    #     putchard(43)  # '+'
    # else
    #     putchard(42); # '*'
    # ...
    # ready> printdensity(1): printdensity(2): printdensity(3):
    #     printdensity(4): printdensity(5): printdensity(9):
    #     putchard(10);
    # **++.
    # Evaluated to 0.000000

    # # Determine whether the specific location diverges.
    # # Solve for z = z^2 + c in the complex plane.
    # def mandelconverger(real imag iters creal cimag)
    # if iters > 255 | (real*real + imag*imag > 4) then
    #     iters
    # else
    #     mandelconverger(real*real - imag*imag + creal,
    #                     2*real*imag + cimag,
    #                     iters+1, creal, cimag);

    # # Return the number of iterations required for the iteration to escape
    # def mandelconverge(real imag)
    # mandelconverger(real, imag, 0, real, imag);

    # # Compute and plot the mandelbrot set with the specified 2 dimensional range
    # # info.
    # def mandelhelp(xmin xmax xstep   ymin ymax ystep)
    # for y = ymin, y < ymax, ystep in (
    #     (for x = xmin, x < xmax, xstep in
    #     printdensity(mandelconverge(x,y)))
    #     : putchard(10)
    # )

    # # mandel - This is a convenient helper function for plotting the mandelbrot set
    # # from the specified position with the specified Magnification.
    # def mandel(realstart imagstart realmag imagmag)
    # mandelhelp(realstart, realstart+realmag*78, realmag,
    #             imagstart, imagstart+imagmag*40, imagmag);


    # ready> mandel(-2.3, -1.3, 0.05, 0.07);