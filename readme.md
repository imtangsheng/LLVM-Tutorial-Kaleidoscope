# Implementing a Language with LLVM: Kaleidoscope

[Getting Started with the LLVM System](https://llvm.org/docs/GettingStarted.html)

[Implementing a Language with LLVM 用LLVM开发新语言](https://llvm-tutorial-cn.readthedocs.io/en/latest/)

# Building Clang and Working with the Code

## On Windows Systems for Debain

    // search softname
    apt-cache policy 'clang'
    // install clang
    sudo apt-get install clang
    clang -v
    // using the LLVM JIT, lli
    sudo apt-get install llvm

### Example with clang
1.First, create a simple C file, name it ‘hello.c’:
```
#include <stdio.h>

int main() {
  printf("hello world\n");
  return 0;
}
```
2.Next, compile the C file into a native executable:

    % clang hello.c -o hello

3.Next, compile the C file into an LLVM bitcode file:

    % clang -O3 -emit-llvm hello.c -c -o hello.bc

4.Run the program in both forms. To run the program, use:

    % ./hello
and

    % lli hello.bc

The second examples shows how to invoke the LLVM JIT, lli.

5.Use the utility to take a look at the LLVM assembly code:llvm-dis

    % llvm-dis < hello.bc | less
6.Compile the program to native assembly using the LLC code generator:

    % llc hello.bc -o hello.s
7.Assemble the native assembly language file into a program:

    % /opt/SUNWspro/bin/cc -xarch=v9 hello.s -o hello.native   # On Solaris

    % gcc hello.s -o hello.native                              # On others
8.Execute the native code program:

    % ./hello.native
Note that using clang to compile directly to native code (i.e. when the option is not present) does steps 6/7/8 for you.-emit-llvm

Note:

    //1.预处理(Preprocessing)
    gcc -E test.c -o test.i
    //2.编译(Compilation)
    gcc -S test.i -o test.s
    //3.汇编(Assemble)
    gcc -c test.s -o test.o
    //4.链接(Linking)
    gcc test.o -o test