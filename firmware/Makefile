# ============================================================
# Orchestra Wireless LED Control — Node Firmware Build
# Uses arduino-cli
# ============================================================

SKETCH     := orchestra_node/orchestra_node.ino
FQBN       := arduino:avr:nano:cpu=atmega328old
PORT       ?= /dev/ttyUSB0
BAUD       ?= 115200

# arduino-cli config
CLI        := arduino-cli
OUTPUT_DIR := build

.PHONY: all compile upload monitor clean libs

all: compile

## Install required libraries
libs:
	$(CLI) lib install "FastLED"
	$(CLI) lib install "RF24"

## Compile the sketch
compile:
	$(CLI) compile \
		--fqbn $(FQBN) \
		--output-dir $(OUTPUT_DIR) \
		$(SKETCH)

## Upload to the board (set PORT=... if needed)
upload:
	$(CLI) upload \
		--fqbn $(FQBN) \
		--port $(PORT) \
		$(SKETCH)

## Compile and upload in one step
flash: compile upload

## Open serial monitor
monitor:
	$(CLI) monitor \
		--port $(PORT) \
		--config baudrate=$(BAUD)

## Remove build artefacts
clean:
	rm -rf $(OUTPUT_DIR)

## Show help
help:
	@echo "Targets:"
	@echo "  libs     - install Arduino libraries (FastLED, RF24)"
	@echo "  compile  - compile sketch"
	@echo "  upload   - upload to board  (PORT=/dev/ttyUSBx)"
	@echo "  flash    - compile + upload"
	@echo "  monitor  - open serial monitor"
	@echo "  clean    - remove build artefacts"
	@echo ""
	@echo "Example:"
	@echo "  make flash PORT=/dev/ttyUSB0"
