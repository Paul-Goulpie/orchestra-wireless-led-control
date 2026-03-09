# Orchestra Wireless LED Control — Architecture

## System overview

```
QLC+ (sACN / E1.31 — unicast or multicast UDP port 5568)
        |
    Ethernet / Wi-Fi
        |
  orchgateway  (Orange Pi Zero 3 + NRF24L01+PA+LNA via SPI)
        |
   2.4 GHz broadcast  (1 Mbps, channel 76, no ACK)
        |
  Up to 60 x Arduino Nano Nodes (RF-Nano)
        |
  WS2812B LED strip (10 LEDs / node)
```

The gateway receives DMX universes from QLC+ via sACN (E1.31) and sends
one 32-byte radio frame per node. Only nodes whose RGB values have changed
receive an immediate update (differential mode); all other nodes are
refreshed once per second. Each node filters on its own address and applies
the embedded RGB data directly to the LED strip.

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

### Node (Arduino)

| Library | Purpose | Install name |
|---------|---------|-------------|
| FastLED | WS2812B control | `FastLED` |
| RF24    | NRF24L01+ driver | `RF24` |

### Gateway (Linux / C)

| Library | Purpose | Install |
|---------|---------|---------|
| RF24    | NRF24L01+ Linux driver (C++) | build from source — see `gateway/docs/INSTALL.md` |
| cJSON   | JSON config parsing | `sudo apt-get install libcjson-dev` |

---

## Gateway — orchgateway

### Configuration file (`/etc/orchgateway.json`)

JSON file with three sections:

```jsonc
{
  "radio": {
    "spi_device":   "/dev/spidev0.0",   // SPI bus device
    "spi_speed_hz": 10000000,            // SPI clock speed (max 10 MHz)
    "ce_pin":       25,                  // GPIO BCM pin for NRF24 CE
    "channel":      76,                  // RF channel (0-125)
    "data_rate":    "1mbps",             // "250kbps" | "1mbps" | "2mbps"
    "repeat_count": 1                    // extra TX repetitions per packet
  },
  "network": {
    "sacn_timeout_ms":     2000,         // silence → blackout threshold
    "refresh_interval_ms": 1000          // periodic re-send interval
  },
  "universes": [
    { "name": "universe_1", "id": 1, "multicast": "239.255.0.1", "port": 5568 }
  ],
  "nodes": [
    {
      "name":        "musician_01",
      "address":     1,                  // 1-63
      "num_leds":    10,
      "universe_id": 1,                  // which sACN universe carries this node
      "dmx_start":   1                   // 1-indexed DMX channel in the universe
    }
  ]
}
```

### DMX channel mapping

Each node occupies `num_leds × 3` consecutive DMX channels starting at
`dmx_start` within `universe_id`.

```
Node address 1: universe 1, channels 1-30   (10 LEDs)
Node address 2: universe 1, channels 31-60
...
Node address 17: universe 1, channels 481-510
Node address 18: universe 2, channels 1-30   (new universe)
```

Use QLC+ to map fixtures to the appropriate universe/start-channel.

### Sending strategy

1. **Differential**: when a sACN packet arrives and changes a node's RGB
   state, that node is immediately transmitted via radio.
2. **Refresh**: every `refresh_interval_ms` (default 1 s), all nodes that
   have been initialised but have no pending update are re-sent to
   compensate for any missed radio frames.
3. **Blackout**: if no sACN packet arrives within `sacn_timeout_ms`
   (default 2 s), all-zero packets are sent to every node.

### Radio parameters (gateway TX)

| Parameter | Value |
|-----------|-------|
| Pipe address | `0xE8E8F0F0E1` (matches node RX pipe) |
| Auto-ACK | Disabled (broadcast) |
| PA level | MAX |
| Payload | 32 bytes fixed |
| Repeats | 1 + `repeat_count` sends per packet |

### Statistics (logged every 30 s)

- `sacn_rx` — sACN packets received
- `sacn_skipped` — packets with no matching node / superseded
- `net_timeout_count` — number of sACN timeout events
- `radio_tx_total` — total radio packets transmitted
- `radio_tx_failed` — transmission failures
- `radio_refresh_total` — refresh packets sent
