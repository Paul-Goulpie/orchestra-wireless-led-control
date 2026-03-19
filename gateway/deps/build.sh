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
