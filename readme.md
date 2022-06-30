# Implementing a Language with LLVM: Kaleidoscope
# 用LLVM开发新语言

[Getting Started with the LLVM System](https://llvm.org/docs/GettingStarted.html)

[My First Language Frontend with LLVM Tutorial](https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/index.html)

# 目录
- [编译和安装LLVM](#building-clang-and-working-with-the-code)
- [第一章 教程简介与词法分析器 第二章 实现语法分析器和AST 完整源码](#第一章-教程简介与词法分析器-第二章-实现语法分析器和ast-完整源码)
- [第三章 LLVM IR代码生成](#第三章-llvm-ir代码生成)
- [第四章 添加JIT和优化支持](#第四章-添加jit和优化支持)
- [第五章 对语言进行扩展：控制流程](#第五章-对语言进行扩展控制流程)
- [第六章 对语言进行扩展：用户自定义运算符](#第六章-对语言进行扩展用户自定义运算符)
- [第七章 对语言进行扩展：可变变量](#第七章-对语言进行扩展可变变量)
- [第八章 结论及LLVM相关的其他有用内容](#第八章-结论及llvm相关的其他有用内容)


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

## 第一章 教程简介与词法分析器 第二章 实现语法分析器和AST 完整源码

[完整源码](https://llvm-tutorial-cn.readthedocs.io/en/latest/chapter-2.html#chapter-2-full-code)

```
# Compile
clang++ -g -O3 toy.cpp `llvm-config --cxxflags`
# Run
./a.out
```

## 第三章 LLVM IR代码生成

```
# Compile
clang++ -g -O3 toy.cpp `llvm-config --cxxflags --ldflags --system-libs --libs core` -o toy
# Run
./toy
```

## 第四章 添加JIT和优化支持
## 第五章 对语言进行扩展：控制流程
## 第六章 对语言进行扩展：用户自定义运算符
## 第七章 对语言进行扩展：可变变量
## 第八章 结论及LLVM相关的其他有用内容