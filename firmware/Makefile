# ============================================================
# Orchestra Wireless LED Control — Node Firmware Build
# Uses arduino-cli
# ============================================================

SKETCH     := .
FQBN       := arduino:avr:nano:cpu=atmega328
PORT       ?= /dev/ttyUSB0
BAUD       ?= 115200
CLI        := arduino-cli
OUTPUT_DIR := build

# ============================================================
#  arduino-cli detection
# ============================================================
ARDUINO_CLI_BIN := $(shell which $(CLI) 2>/dev/null || echo /usr/local/bin/$(CLI) 2>/dev/null)
# Re-check that the resolved path actually exists
ARDUINO_CLI_BIN := $(shell test -x "$(ARDUINO_CLI_BIN)" && echo "$(ARDUINO_CLI_BIN)")

define ARDUINO_CLI_INSTALL_MSG

  ERROR: arduino-cli not found in PATH.

  Install (Linux):
    curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
    sudo mv bin/arduino-cli /usr/local/bin/

  First-time setup:
    arduino-cli config init
    arduino-cli core update-index
    arduino-cli core install arduino:avr   # ATmega328P support

  Then install project libraries:
    make libs

endef
export ARDUINO_CLI_INSTALL_MSG

.PHONY: check-cli all compile upload flash monitor clean libs help

check-cli:
	@test -n "$(ARDUINO_CLI_BIN)" || { echo "$$ARDUINO_CLI_INSTALL_MSG"; exit 1; }

# ============================================================
#  Targets
# ============================================================
all: compile

## Install required libraries
libs: check-cli
	$(CLI) lib install "FastLED"
	$(CLI) lib install "RF24"

## Compile the sketch
compile: check-cli
	$(CLI) compile \
		--fqbn $(FQBN) \
		--output-dir $(OUTPUT_DIR) \
		$(SKETCH)

## Upload to the board
upload: check-cli
	$(CLI) upload \
		--fqbn $(FQBN) \
		--port $(PORT) \
		$(SKETCH)

## Compile + upload in one step
flash: compile upload

## Open serial monitor
monitor: check-cli
	$(CLI) monitor \
		--port $(PORT) \
		--config baudrate=$(BAUD)

## Remove build artefacts
clean:
	rm -rf $(OUTPUT_DIR)

## Show help
help:
	@echo ""
	@echo "Targets:"
	@echo "  libs     - install Arduino libraries (FastLED, RF24)"
	@echo "  compile  - compile sketch"
	@echo "  upload   - upload to board"
	@echo "  flash    - compile + upload"
	@echo "  monitor  - open serial monitor"
	@echo "  clean    - remove build artefacts"
	@echo ""
	@echo "Variables:"
	@echo "  PORT=$(PORT)   serial port"
	@echo "  BAUD=$(BAUD)      serial baud rate"
	@echo ""
	@echo "Example:"
	@echo "  make flash PORT=/dev/ttyUSB0"
	@echo ""
