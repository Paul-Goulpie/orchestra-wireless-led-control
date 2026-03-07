# Orchestra Wireless LED Control — Architecture

## System overview

```
QLC+ (DMX / Art-Net / sACN)
        |
    Ethernet
        |
  Gateway (Raspberry Pi + NRF24L01+)
        |
   2.4 GHz broadcast
        |
  Up to 60 x Arduino Nano Nodes (RF-Nano)
        |
  WS2812B LED strip (10 LEDs / node)
```

The gateway receives DMX universes from QLC+ and rebroadcasts them over
2.4 GHz radio as a simple binary protocol. Every node silently receives
all broadcasts and extracts only the RGB values that concern its own
address.

---

## Hardware — Node

**Board:** RF-Nano (ATmega328P QFN32 + NRF24L01+ 2.4 GHz on-board)

### Pin assignment

| Pin | Function | Notes |
|-----|----------|-------|
| D2  | WS2812B data | LED strip signal |
| D3  | Test mode jumper | Pull LOW to activate |
| D4  | Address bit 0 (LSB) | Pull LOW = bit set |
| D5  | Address bit 1 | |
| D6  | Address bit 2 | |
| D7  | Address bit 3 | |
| D8  | Address bit 4 | |
| A0  | Address bit 5 (MSB) | |
| D9  | NRF24L01 CSN | Built-in on RF-Nano |
| D10 | NRF24L01 CE  | Built-in on RF-Nano |
| D11 | SPI MOSI | Built-in |
| D12 | SPI MISO | Built-in |
| D13 | SPI SCK  | Built-in |

All jumper pins use the internal pull-up resistor. Closing a jumper to GND
activates the corresponding bit/mode. Changes are picked up on every loop
iteration — no reset required.

### Address encoding

6 address bits → values 0–63. Valid node addresses: **1 to 60**.
Address 0 means unconfigured (node receives but ignores DMX data).

### Test mode (chenillard)

When `D3` is pulled LOW, the node ignores radio traffic and runs a blue
LED chase pattern (one LED on at a time, 100 ms per step). This mode
is useful to verify wiring before deployment.

---

## Radio protocol

| Parameter | Value |
|-----------|-------|
| Chip | NRF24L01+ |
| Frequency | 2.4 GHz, channel 76 |
| Data rate | 250 kbps |
| Payload size | 32 bytes (fixed) |
| Auto-ACK | Disabled (broadcast) |
| Pipe address | `0xE8E8F0F0E1` |

The gateway writes to this pipe. All nodes open it as reading pipe 1.
The 32-byte NRF24L01 payloads are **transparent transport**: the gateway
fragments its logical packet into consecutive 32-byte chunks; the node
reassembles by feeding every byte through the parser.

---

## Packet format

```
+------------------+----------+------------+------------------+----------+
| magic            | pkt_len  | id_univers |  valeurs_DMX     | CRC32    |
| "START_LEDS"     | uint16   | uint8      | 0 – 512 bytes    | uint32   |
| 10 bytes         | 2 bytes  | 1 byte     |                  | 4 bytes  |
+------------------+----------+------------+------------------+----------+
```

- **magic** — ASCII string `START_LEDS` (no NUL terminator).
- **pkt_len** — total packet size in bytes (magic + pkt_len + id_univers +
  DMX payload + CRC32), little-endian. Full universe: `17 + 512 = 529`.
- **id_univers** — 0-based universe index.
- **valeurs_DMX** — raw DMX channel values, 3 bytes per LED (R, G, B).
  A node's data must not span two universes.
- **CRC32** — covers all preceding bytes (magic through last DMX byte),
  little-endian.

---

## DMX addressing model

```
CHANNELS_PER_NODE  = 10 LEDs × 3 channels = 30
NODES_PER_UNIVERSE = floor(512 / 30)       = 17
```

| Universe | Nodes |
|----------|-------|
| 0 | 1 – 17  |
| 1 | 18 – 34 |
| 2 | 35 – 51 |
| 3 | 52 – 60 (9 nodes, 270 DMX channels) |

For node address **N** (1-based):

```
my_universe       = (N − 1) / NODES_PER_UNIVERSE     (integer division)
first_node_in_univ = my_universe × NODES_PER_UNIVERSE + 1
dmx_offset        = (N − first_node_in_univ) × CHANNELS_PER_NODE
```

**Example — node 42:**

```
my_universe        = (42 − 1) / 17 = 2
first_node_in_univ = 2 × 17 + 1    = 35
dmx_offset         = (42 − 35) × 30 = 210
LED data           = DMX bytes [210 … 239]
```

---

## Node firmware state machine

The parser processes one byte at a time:

```
PS_MAGIC ──(10 magic bytes matched)──> PS_LEN_LOW
PS_LEN_LOW ─────────────────────────> PS_LEN_HIGH
PS_LEN_HIGH ─(valid length)──────────> PS_UNIVERSE
PS_UNIVERSE ─(dmx_len > 0)───────────> PS_DMX_DATA
            └(dmx_len == 0)──────────> PS_CRC
PS_DMX_DATA ─(all DMX bytes received)> PS_CRC
PS_CRC ─────(4 bytes received)───────> process_packet() → PS_MAGIC
```

CRC is accumulated as bytes arrive (no separate second pass needed).

Any framing error (bad magic byte, invalid length) resets the machine to
`PS_MAGIC`.

---

## Timeout

If no packet passes CRC validation within **2 seconds**, all LEDs are
turned off. This ensures the band goes dark if the gateway stops
transmitting or the radio link is lost.

---

## Build & flash

```bash
cd firmware
make libs        # install FastLED + RF24 (once)
make flash PORT=/dev/ttyUSB0
make monitor PORT=/dev/ttyUSB0
```

---

## Libraries

| Library | Purpose | Arduino library name |
|---------|---------|----------------------|
| FastLED | WS2812B control | `FastLED` |
| RF24    | NRF24L01+ driver | `RF24` |
