.RECIPEPREFIX = >

HEADER_PATH = /usr/include/llvm-3.8/ -I/usr/include/llvm-c-3.8/
# CPATH  = /usr/include/llvm-c-3.8/ : /usr/include/llvm-3.8/

test:
> clang++ -g -O3 main.cpp -o test -I$(HEADER_PATH)
clean:
> @rm -f test *.o