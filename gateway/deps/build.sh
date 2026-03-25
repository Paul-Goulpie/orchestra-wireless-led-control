#!/usr/bin/env bash

SCRIPT_PATH=$(dirname $0)

if [ ! -f ${SCRIPT_PATH}/cJSON/.git ]; then
	git submodule init
	git submodule update ${SCRIPT_PATH}/cJSON
fi
cmake -B ${SCRIPT_PATH}/build/cJSON ${SCRIPT_PATH}/cJSON
cmake --build ${SCRIPT_PATH}/build/cJSON -j $(nproc)
sudo cmake --install ${SCRIPT_PATH}/build/cJSON

if [ ! -f ${SCRIPT_PATH}/sACN/.git ]; then
	git submodule init
	git submodule update ${SCRIPT_PATH}/sACN
fi
cmake -B ${SCRIPT_PATH}/build/sACN ${SCRIPT_PATH}/sACN
cmake --build ${SCRIPT_PATH}/build/sACN -j $(nproc)
sudo cmake --install ${SCRIPT_PATH}/build/sACN

if [ ! -f ${SCRIPT_PATH}/RF24/.git ]; then
	git submodule init
	git submodule update ${SCRIPT_PATH}/RF24
fi
cmake -B ${SCRIPT_PATH}/build/RF24 ${SCRIPT_PATH}/RF24
cmake --build ${SCRIPT_PATH}/build/RF24 -j $(nproc)
sudo cmake --install ${SCRIPT_PATH}/build/RF24
