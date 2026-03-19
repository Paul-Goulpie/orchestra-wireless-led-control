#!/usr/bin/env bash

if [ ! -f cJSON/.git ]; then
	git submodule init
	git submodule update cJSON
fi
cmake -B build/cJSON cJSON
cmake --build build/cJSON -j $(nproc)
sudo cmake --install build/cJSON

if [ ! -f sACN/.git ]; then
	git submodule init
	git submodule update cJSON
fi
cmake -B build/sACN sACN
cmake --build build/sACN -j $(nproc)
sudo cmake --install build/sACN

if [ ! -f RF24/.git ]; then
	git submodule init
	git submodule update cJSON
fi
cmake -B build/RF24 RF24
cmake --build build/RF24 -j $(nproc)
sudo cmake --install build/RF24
