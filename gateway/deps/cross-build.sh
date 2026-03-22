#!/usr/bin/env bash

CROSS_COMPILE=${CROSS_COMPILE:-aarch64-linux-gnu-}

echo "start with
	CROSS_COMPILE : ${CROSS_COMPILE}"

export CC=${CROSS_COMPILE}gcc
export CXX=${CROSS_COMPILE}g++
export ASM=${CROSS_COMPILE}as

./build.sh
