.RECIPEPREFIX = >

test:
> clang++ -g -O3 toy.cpp `llvm-config --cxxflags --ldflags --system-libs --libs core` -o test

clean:
> @rm -f test *.o