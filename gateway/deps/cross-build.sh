#!/usr/bin/env bash

SCRIPT_PATH=$(dirname $0)
CROSS_PREFIX=${CROSS_PREFIX:-aarch64-linux-gnu-}

echo "start with
	CROSS_PREFIX : ${CROSS_PREFIX}
	SCRIPT_PATH  : ${SCRIPT_PATH}"

export CC=${CROSS_PREFIX}gcc
export CXX=${CROSS_PREFIX}g++
export ASM=${CROSS_PREFIX}as

${SCRIPT_PATH}/build.sh
