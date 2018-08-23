#!/bin/sh
cd build
rm -r *
cmake -G "NMake Makefiles" -DCMAKE_TOOLCHAIN_FILE=../IDE/IAR-EWARM/iar-toolchain.cmake ..
cmake --build .
cd ..