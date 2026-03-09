APP      := orchgateway
CC       := gcc
CXX      := g++

CFLAGS   := -Wall -Wextra -O2 -std=c11   -D_POSIX_C_SOURCE=200809L
CXXFLAGS := -Wall -Wextra -O2 -std=c++14

LDFLAGS  :=
LIBS     := -lrf24 -lcjson -lpthread

SRC_DIR  := src
RES_DIR  := resources
BUILD_DIR := build

# Resource: default config embedded as a C byte array
RESOURCE_JSON := $(RES_DIR)/default_config.json
RESOURCE_H    := $(SRC_DIR)/default_config_data.h

# Source files
SRCS_C   := $(wildcard $(SRC_DIR)/*.c)
SRCS_CPP := $(wildcard $(SRC_DIR)/*.cpp)

OBJS := $(patsubst $(SRC_DIR)/%.c,   $(BUILD_DIR)/%.o, $(SRCS_C))   \
        $(patsubst $(SRC_DIR)/%.cpp,  $(BUILD_DIR)/%.o, $(SRCS_CPP))

DEPS := $(OBJS:.o=.d)

# Cross-compilation: make CROSS_PREFIX=aarch64-linux-gnu-
ifdef CROSS_PREFIX
    CC  := $(CROSS_PREFIX)gcc
    CXX := $(CROSS_PREFIX)g++
endif

# RF24 include paths (common install locations)
RF24_INC := $(firstword $(wildcard /usr/include/RF24 /usr/local/include/RF24))

# --------------------------------------------------------------------------

.PHONY: all clean install check-deps help

all: check-deps $(BUILD_DIR)/$(APP)

# ---- Dependency checks --------------------------------------------------------

check-deps:
	@command -v $(CC)  >/dev/null 2>&1 || \
	  { echo "ERROR: $(CC) not found. Install build-essential: sudo apt-get install build-essential"; exit 1; }
	@command -v xxd    >/dev/null 2>&1 || \
	  { echo "ERROR: xxd not found. Install: sudo apt-get install xxd"; exit 1; }
	@pkg-config --exists libcjson 2>/dev/null || \
	  { echo "ERROR: cJSON library not found."; \
	    echo "  Install: sudo apt-get install libcjson-dev"; exit 1; }
	@test -n "$(RF24_INC)" || \
	  { echo "ERROR: RF24 library headers not found."; \
	    echo "  Install: see gateway/docs/INSTALL.md"; exit 1; }

# ---- Embedded resource -------------------------------------------------------

$(RESOURCE_H): $(RESOURCE_JSON)
	@mkdir -p $(SRC_DIR)
	xxd -i $< | sed 's/resources_default_config_json/default_config_data/g' > $@
	@echo "Generated $@"

# ---- Compilation -------------------------------------------------------------

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(RESOURCE_H)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(shell pkg-config --cflags libcjson 2>/dev/null) \
	      -MMD -MP -I$(SRC_DIR) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp $(RESOURCE_H)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -MMD -MP -I$(SRC_DIR) -I$(RF24_INC) -c $< -o $@

-include $(DEPS)

# ---- Linking -----------------------------------------------------------------

$(BUILD_DIR)/$(APP): $(OBJS)
	$(CXX) $(LDFLAGS) $^ \
	       $(shell pkg-config --libs libcjson 2>/dev/null || echo -lcjson) \
	       -lrf24 -lpthread \
	       -o $@
	@echo "Built: $@"

# ---- Install -----------------------------------------------------------------

install: $(BUILD_DIR)/$(APP)
	install -m 755 $(BUILD_DIR)/$(APP) /usr/local/bin/$(APP)
	@test -f /etc/$(APP).json || \
	  install -m 644 $(RES_DIR)/default_config.json /etc/$(APP).json
	install -m 644 systemd/$(APP).service /etc/systemd/system/
	systemctl daemon-reload
	@echo "Installed $(APP). Enable service with: systemctl enable --now $(APP)"

# ---- Misc --------------------------------------------------------------------

clean:
	rm -rf $(BUILD_DIR) $(RESOURCE_H) $(DEPS)

help:
	@echo "Targets:"
	@echo "  all        Build $(APP) (default)"
	@echo "  install    Install binary, config and systemd service"
	@echo "  clean      Remove build artefacts"
	@echo ""
	@echo "Cross-compile: make CROSS_PREFIX=aarch64-linux-gnu-"
	@echo "Custom CC:     make CC=gcc-12 CXX=g++-12"
