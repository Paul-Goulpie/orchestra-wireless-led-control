#!/usr/bin/env bash

if [ ! -f cJSON/.git ]; then
	git submodule init
	git submodule update cJSON
fi

cmake --B cJSON/build cJSON
