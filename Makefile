.RECIPEPREFIX = >

test:
> $(CC) -o test hello.c

clean:
> @rm -f test *.o