#!/usr/bin/env bash

CROSS_PREFIX=${CROSS_PREFIX:-aarch64-linux-gnu-}

echo "start with
	CROSS_PREFIX : ${CROSS_PREFIX}"

export CC=${CROSS_PREFIX}gcc
export CXX=${CROSS_PREFIX}g++
export ASM=${CROSS_PREFIX}as

./build.sh
