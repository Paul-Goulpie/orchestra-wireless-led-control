APP      := orchgateway
CC       := gcc
CXX      := g++

CFLAGS   := -Wall -Wextra -O0 -std=c11 -D_POSIX_C_SOURCE=200809L -g3
CXXFLAGS := -Wall -Wextra -O0 -std=c++14 -g3

SRC_DIR   := src
RES_DIR   := resources
BUILD_DIR := build

# Resource: default config embedded as a C byte array
RESOURCE_JSON := $(RES_DIR)/default_config.json
RESOURCE_H    := $(SRC_DIR)/default_config_data.h

# Source files
SRCS_C   := $(wildcard $(SRC_DIR)/*.c)
SRCS_CPP := $(wildcard $(SRC_DIR)/*.cpp)

OBJS := $(patsubst $(SRC_DIR)/%.c,  $(BUILD_DIR)/%.o, $(SRCS_C))  \
        $(patsubst $(SRC_DIR)/%.cpp, $(BUILD_DIR)/%.o, $(SRCS_CPP))

DEPS := $(OBJS:.o=.d)

# Cross-compilation: make CROSS_PREFIX=aarch64-linux-gnu-
ifdef CROSS_PREFIX
    CC  := $(CROSS_PREFIX)gcc
    CXX := $(CROSS_PREFIX)g++
endif

# ---- Library paths -----------------------------------------------------------

# RF24
RF24_INC := $(firstword $(wildcard /usr/include/RF24 /usr/local/include/RF24))

# sACN + EtcPal (ETC Labs)
SACN_INC := /usr/local/include
SACN_LIB := /usr/local/lib

# cJSON (system package)
CJSON_CFLAGS := $(shell pkg-config --cflags libcjson 2>/dev/null)
CJSON_LIBS   := $(shell pkg-config --libs   libcjson 2>/dev/null || echo -lcjson)

# --------------------------------------------------------------------------

.PHONY: all clean install check-deps help

all: check-deps $(BUILD_DIR)/$(APP)

# ---- Dependency checks -------------------------------------------------------

check-deps:
	@command -v $(CC) >/dev/null 2>&1 || \
	  { echo "ERROR: $(CC) not found. Install: sudo apt-get install build-essential"; exit 1; }
	@command -v xxd >/dev/null 2>&1 || \
	  { echo "ERROR: xxd not found. Install: sudo apt-get install xxd"; exit 1; }
	@pkg-config --exists libcjson 2>/dev/null || \
	  { echo "ERROR: cJSON not found. Install: sudo apt-get install libcjson-dev"; exit 1; }
	@test -n "$(RF24_INC)" || \
	  { echo "ERROR: RF24 headers not found. See gateway/docs/INSTALL.md"; exit 1; }
	@test -f $(SACN_INC)/sacn/receiver.h || \
	  { echo "ERROR: sACN headers not found at $(SACN_INC)/sacn/."; \
	    echo "  Build from source: https://github.com/ETCLabs/sACN"; \
	    echo "  See gateway/docs/INSTALL.md"; exit 1; }
	@test -f $(SACN_LIB)/libsACN.a || \
	  { echo "ERROR: libsACN.a not found at $(SACN_LIB)."; \
	    echo "  Build from source: https://github.com/ETCLabs/sACN"; exit 1; }

# ---- Embedded resource -------------------------------------------------------

$(RESOURCE_H): $(RESOURCE_JSON)
	@mkdir -p $(SRC_DIR)
	xxd -i $< | sed 's/resources_default_config_json/default_config_data/g' > $@
	@echo "Generated $@"

# ---- Compilation -------------------------------------------------------------

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(RESOURCE_H)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CJSON_CFLAGS) \
	      -I$(SRC_DIR) -I$(SACN_INC) \
	      -MMD -MP -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp $(RESOURCE_H)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) \
	       -I$(SRC_DIR) -I$(RF24_INC) -I$(SACN_INC) \
	       -MMD -MP -c $< -o $@

-include $(DEPS)

# ---- Linking -----------------------------------------------------------------
# Link order: sACN depends on EtcPal; both are static.
# System libs: pthread (sACN threads), m (math), rt (clock_gettime on older libc)

$(BUILD_DIR)/$(APP): $(OBJS)
	$(CXX) $^ \
	       -L$(SACN_LIB) -lsACN -lEtcPal \
	       -lrf24 \
	       $(CJSON_LIBS) \
	       -lpthread -lm -lrt \
	       -o $@
	@echo "Built: $@"

# ---- Install -----------------------------------------------------------------

install: $(BUILD_DIR)/$(APP)
	install -m 755 $(BUILD_DIR)/$(APP) /usr/local/bin/$(APP)
	@test -f /etc/$(APP).json || \
	  install -m 644 $(RES_DIR)/default_config.json /etc/$(APP).json
	install -m 644 systemd/$(APP).service /etc/systemd/system/
	systemctl daemon-reload
	@echo "Installed $(APP). Enable with: systemctl enable --now $(APP)"

# ---- Misc --------------------------------------------------------------------

clean:
	rm -rf $(BUILD_DIR) $(RESOURCE_H)

help:
	@echo "Targets:"
	@echo "  all        Build $(APP) (default)"
	@echo "  install    Install binary, config and systemd service"
	@echo "  clean      Remove build artefacts"
	@echo ""
	@echo "Cross-compile: make CROSS_PREFIX=aarch64-linux-gnu-"
	@echo "Custom CC:     make CC=gcc-12 CXX=g++-12"
