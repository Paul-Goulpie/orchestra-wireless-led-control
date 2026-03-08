# Orchestra Wireless LED Control — Architecture

## System overview

```
QLC+ (DMX / Art-Net / sACN)
        |
    Ethernet
        |
  Gateway (Raspberry Pi + NRF24L01+)
        |
   2.4 GHz broadcast  (1 Mbps, channel 76)
        |
  Up to 60 x Arduino Nano Nodes (RF-Nano)
        |
  WS2812B LED strip (10 LEDs / node)
```

The gateway receives DMX universes from QLC+ and sends one 32-byte radio
frame per node per lighting frame. Each node filters on its own address
and applies the embedded RGB data directly to the LED strip.

---

## Hardware — Node

**Board:** RF-Nano (ATmega328P QFN32 + NRF24L01+ 2.4 GHz on-board)

### Pin assignment

| Pin | Function | Notes |
|-----|----------|-------|
| **A0** | **WS2812B data** | Isolated on analog side |
| D2  | Test mode jumper | Pull LOW → chenillard mode |
| D3  | Address bit 0 (LSB) | Active LOW + internal pull-up |
| D4  | Address bit 1 | |
| D5  | Address bit 2 | |
| D6  | Address bit 3 | |
| D7  | Address bit 4 | |
| D8  | Address bit 5 (MSB) | 6 bits → addresses 1-63 |
| D9  | NRF24L01 CSN | Built-in on RF-Nano |
| D10 | NRF24L01 CE  | Built-in on RF-Nano |
| D11-13 | SPI | Built-in |

Jumper changes take effect immediately without reset.

### Address encoding

6 bits → valid node addresses **1–60** (0 = unconfigured, ignored).

### Test mode (chenillard)

Pulling `D3` LOW disables radio processing and runs a blue LED chase
(one LED lit at a time, 100 ms/step). Used to verify wiring before deployment.

---

## Radio protocol

| Parameter | Value |
|-----------|-------|
| Chip | NRF24L01+ |
| Frequency | 2.4 GHz, channel 76 |
| Data rate | **1 Mbps** |
| Payload size | 32 bytes (fixed) |
| Auto-ACK | Disabled (broadcast) |
| CRC | Hardware CRC-16 (NRF24L01 built-in) |
| Pipe address | `0xE8E8F0F0E1` |

The gateway writes to the shared pipe; all nodes open it as reading pipe 1.
Software CRC is not used — the radio's built-in CRC-16 provides integrity.

---

## Packet format — `node_packet_v1`

```c
struct __attribute__((packed)) node_packet_v1 {
    uint8_t dst_addr;   /* destination node address (1-63) */
    uint8_t frame_id;   /* wrapping counter, incremented per frame */
    uint8_t rgb[30];    /* 10 LED × 3 bytes (R, G, B) */
};
/* sizeof == 32 bytes == NRF24L01 payload size */
```

- **dst_addr** — node filters any packet where `dst_addr != my_address`.
- **frame_id** — monotonically increasing uint8 (wraps 255→0). Used to
  detect duplicates, late packets, and dropped frames.
- **rgb[30]** — raw RGB values; `rgb[i*3]` = R, `rgb[i*3+1]` = G,
  `rgb[i*3+2]` = B for LED `i`.

---

## frame_id logic

Without a frame counter, a node cannot distinguish a new frame from a
retransmission or a late-arriving packet (common in ISM-band environments).

```
diff = (int8_t)(received.frame_id - last_frame_id)   // signed wrap arithmetic

diff == 0  →  duplicate      (discard, count rx_duplicate)
diff  < 0  →  late / reorder (discard, count rx_late)
diff  > 1  →  gap detected   (accept, frames_lost += diff - 1)
diff == 1  →  normal next    (accept)
```

---

## Radio statistics (logged every 10 s)

```
---- Radio stats (10 s) ----
  rx_total       : total payloads pulled from radio FIFO
  rx_wrong_addr  : payloads for other nodes
  rx_for_us      : accepted + duplicate + late
    accepted      : new frames applied to LEDs
    duplicate     : same frame_id as previous
    late          : frame_id older than previous
  frames_lost    : estimated missed frames (frame_id gaps)
----------------------------
```

---

## DMX mapping

```
CHANNELS_PER_NODE  = 10 LEDs × 3 ch = 30
```

The gateway maps each QLC+ DMX channel block to the `rgb[30]` field of
the corresponding node's packet. Multi-universe management is handled
entirely on the gateway side; nodes are unaware of DMX universes.

---

## Timeout

If no packet passes address/frame validation within **2 seconds**,
all LEDs are turned off.

---

## Build & flash

### Prerequisites

#### 1. Install arduino-cli

```bash
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
sudo mv bin/arduino-cli /usr/local/bin/
```

#### 2. Init and install AVR core

```bash
arduino-cli config init
arduino-cli core update-index
arduino-cli core install arduino:avr   # ATmega328P / Nano support
```

#### 3. Install project libraries

```bash
cd firmware
make libs   # installs FastLED and RF24
```

### Build commands

```bash
cd firmware
make compile               # compile only
make flash PORT=/dev/ttyUSB0   # compile + upload
make monitor PORT=/dev/ttyUSB0 # open serial monitor (115200 baud)
make clean                 # remove build artefacts
```

If `arduino-cli` is not found, `make` will print the full install
instructions automatically.

---

## Libraries

| Library | Purpose | Install name |
|---------|---------|-------------|
| FastLED | WS2812B control | `FastLED` |
| RF24    | NRF24L01+ driver | `RF24` |
