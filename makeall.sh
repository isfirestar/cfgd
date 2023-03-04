#!/bin/bash

echo "start make jess.i64"
make clean && make TARGET=jess.i64
make clean && make TARGET=jess.i64.debug build=debug
echo "make jess.i64 end"

echo "start make jess.arm32"
make clean && make TARGET=jess.arm32 arch=arm
make clean && make TARGET=jess.arm32.debug arch=arm build=debug
echo "make jess.arm32 end"

echo "start make jess.arm64"
make clean && make TARGET=jess.arm64 arch=arm64
make clean && make TARGET=jess.arm64.debug build=debug arch=arm64
echo "make jess.arm64 end"

