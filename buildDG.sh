#!/usr/bin/env bash

mkdir -p build && cd build
cmake -DLLVM_SRC_PATH=/usr/lib/llvm-10 -DLLVM_BUILD_PATH=/usr/lib/llvm-10/build -DLLVM_DIR=/usr/lib/llvm-10/build/share/llvm/cmake ..
