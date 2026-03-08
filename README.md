# Orchestra Wireless LED Control

A distributed wireless LED control system for live performances,
enabling real-time DMX (QLC+) control of individually addressable
wearable LED nodes over radio.

## Overview

This project provides a scalable architecture to control wearable LED
strips used by up to 60 musicians during live performances.

Each musician wears a small LED band (10 WS2812B LEDs) driven by an
RF-Nano (Arduino Nano + NRF24L01+). A central gateway receives DMX data
from QLC+ (via Art-Net or sACN) and sends one 32-byte radio frame per
node per lighting tick.

------------------------------------------------------------------------

## System Architecture

```
QLC+ (DMX / Art-Net / sACN)
        |  Ethernet
  Gateway (Raspberry Pi + NRF24L01+)
        |  2.4 GHz broadcast — 1 Mbps
  Up to 60 × RF-Nano Nodes
        |
  WS2812B LED strips (10 LEDs / node)
```

------------------------------------------------------------------------

## Features

- Up to 60 independent wireless LED nodes
- 10 RGB LEDs per node (individually addressable)
- Compact 32-byte unicast radio frame per node
- frame_id for duplicate / late-packet detection
- Hardware CRC-16 (NRF24L01 built-in) — no software CRC overhead
- Jumper-configurable node address (no firmware recompile)
- Jumper-activated test mode (blue LED chenillard)
- Radio statistics printed every 10 s over UART
- 2 s RX timeout → LEDs off on signal loss

------------------------------------------------------------------------

## Hardware Components

### Gateway

- Raspberry Pi (any model with GPIO)
- NRF24L01+ module

### Node (per musician)

- RF-Nano (ATmega328P + NRF24L01+ integrated)
- WS2812B LED strip — 10 LEDs
- 6 address jumpers + 1 test-mode jumper
- 5 V power supply

------------------------------------------------------------------------

## Radio Protocol

| Parameter | Value |
|-----------|-------|
| Data rate | 1 Mbps |
| Channel | 76 (2.476 GHz) |
| Payload | 32 bytes fixed |
| CRC | Hardware CRC-16 |

### Packet — `node_packet_v1`

```c
struct __attribute__((packed)) node_packet_v1 {
    uint8_t dst_addr;  /* node address 1-63 */
    uint8_t frame_id;  /* wrapping frame counter */
    uint8_t rgb[30];   /* 10 LED × R,G,B */
};
```

See [docs/architecture.md](docs/architecture.md) for full protocol details.

------------------------------------------------------------------------

## Quick Start

### 1. Install arduino-cli

```bash
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
sudo mv bin/arduino-cli /usr/local/bin/
arduino-cli config init
arduino-cli core update-index
arduino-cli core install arduino:avr
```

### 2. Build and flash

```bash
cd firmware
make libs                        # install FastLED + RF24 (once)
make flash PORT=/dev/ttyUSB0
make monitor PORT=/dev/ttyUSB0   # view UART logs at 115200 baud
```

> If `arduino-cli` is missing, `make` will print install instructions.

------------------------------------------------------------------------

## Repository Structure

```
/firmware   → Arduino Nano node firmware + Makefile
/gateway    → DMX receiver + RF broadcaster (Raspberry Pi)
/docs       → Protocol specification and architecture
/hardware   → Schematics and wiring diagrams
```

------------------------------------------------------------------------

## Status

Project in active development.

------------------------------------------------------------------------

## License

[GPLv2](LICENSE)
