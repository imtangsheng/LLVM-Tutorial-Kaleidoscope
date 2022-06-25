#!/bin/bash

function test() {
    ./test
}

make clean
make -s test

test

echo "All tests passed"