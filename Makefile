.RECIPEPREFIX = >

test:
> clang++ -g main.cpp `llvm-config --cxxflags --ldflags --system-libs --libs core orcjit native` -O3 -o test

clean:
> @rm -f test *.o