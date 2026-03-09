#!/usr/bin/env python3
"""
gen_config.py — Generate orchgateway JSON configuration for N musicians.

Rules:
  - Each node occupies CHANNELS_PER_NODE = 30 DMX channels (10 LEDs × RGB).
  - sACN universes hold at most 512 channels.
  - Nodes are packed into universes without spanning universe boundaries:
      nodes_per_universe = floor(512 / 30) = 17
      (17 × 30 = 510 channels used, 2 left unused per universe)
  - Multicast address follows E1.31 convention:
      239.255.<universe_id >> 8>.<universe_id & 0xFF>

Usage:
  ./gen_config.py                      # 60 musicians → stdout
  ./gen_config.py -n 30               # 30 musicians → stdout
  ./gen_config.py -n 60 -o /etc/orchgateway.json
"""

import argparse
import json
import math
import sys

# ── Constants ────────────────────────────────────────────────────────────────

CHANNELS_PER_NODE    = 30    # 10 LEDs × 3 channels (R, G, B)
MAX_UNIVERSE_SLOTS   = 512   # sACN universe size
NODES_PER_UNIVERSE   = MAX_UNIVERSE_SLOTS // CHANNELS_PER_NODE   # 17
MAX_NODES            = 63    # NRF24 address space (1–63)

# ── Helpers ──────────────────────────────────────────────────────────────────

def multicast_for(universe_id: int) -> str:
    """E1.31 multicast address for a given universe id."""
    return f"239.255.{universe_id >> 8}.{universe_id & 0xFF}"


# ── Config generation ─────────────────────────────────────────────────────────

def generate(num_musicians: int) -> dict:
    num_universes = math.ceil(num_musicians / NODES_PER_UNIVERSE)

    universes = []
    nodes     = []

    for u in range(num_universes):
        universe_id = u + 1
        first_addr  = u * NODES_PER_UNIVERSE + 1
        last_addr   = min(first_addr + NODES_PER_UNIVERSE - 1, num_musicians)

        universes.append({
            "name":      f"universe_{universe_id}",
            "id":        universe_id,
            "multicast": multicast_for(universe_id),
            "port":      5568,
        })

        for addr in range(first_addr, last_addr + 1):
            pos       = addr - first_addr          # 0-indexed within universe
            dmx_start = pos * CHANNELS_PER_NODE + 1

            nodes.append({
                "name":        f"musician_{addr:02d}",
                "address":     addr,
                "num_leds":    10,
                "universe_id": universe_id,
                "dmx_start":   dmx_start,
            })

    return {
        "radio": {
            "spi_device":   "/dev/spidev0.0",
            "spi_speed_hz": 10000000,
            "ce_pin":       25,
            "channel":      76,
            "data_rate":    "1mbps",
            "repeat_count": 1,
            "irq_pin":      -1,
        },
        "network": {
            "sacn_timeout_ms":     2000,
            "refresh_interval_ms": 1000,
        },
        "universes": universes,
        "nodes":     nodes,
    }


# ── Summary ───────────────────────────────────────────────────────────────────

def print_summary(num_musicians: int, config: dict):
    universes     = config["universes"]
    num_universes = len(universes)
    slots_used    = NODES_PER_UNIVERSE * CHANNELS_PER_NODE

    print(f"Musicians   : {num_musicians}", file=sys.stderr)
    print(f"Universes   : {num_universes}", file=sys.stderr)
    print(f"Max nodes/u : {NODES_PER_UNIVERSE}  "
          f"({slots_used}/{MAX_UNIVERSE_SLOTS} slots used, "
          f"{MAX_UNIVERSE_SLOTS - slots_used} unused per full universe)",
          file=sys.stderr)
    print(file=sys.stderr)

    for u in universes:
        uid   = u["id"]
        first = (uid - 1) * NODES_PER_UNIVERSE + 1
        last  = min(uid * NODES_PER_UNIVERSE, num_musicians)
        count = last - first + 1
        print(f"  Universe {uid:2d}  mc={u['multicast']}  "
              f"nodes {first:2d}–{last:2d} ({count} nodes, "
              f"DMX 1–{count * CHANNELS_PER_NODE})",
              file=sys.stderr)


# ── CLI ───────────────────────────────────────────────────────────────────────

def main():
    epilog = (
        f"Packing: {NODES_PER_UNIVERSE} nodes × {CHANNELS_PER_NODE} ch = "
        f"{NODES_PER_UNIVERSE * CHANNELS_PER_NODE} slots per universe "
        f"(of {MAX_UNIVERSE_SLOTS} available).\n"
        f"No node ever spans two universes."
    )

    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=epilog,
    )
    parser.add_argument(
        "-n", "--musicians",
        type=int, default=60,
        metavar="N",
        help=f"Number of musicians / nodes  (1–{MAX_NODES}, default: 60)",
    )
    parser.add_argument(
        "-o", "--output",
        default="-",
        metavar="FILE",
        help="Output file path (default: stdout)",
    )

    args = parser.parse_args()

    if not (1 <= args.musicians <= MAX_NODES):
        parser.error(f"--musicians must be between 1 and {MAX_NODES}")

    config = generate(args.musicians)
    print_summary(args.musicians, config)

    out = json.dumps(config, indent=2) + "\n"

    if args.output == "-":
        sys.stdout.write(out)
    else:
        with open(args.output, "w") as f:
            f.write(out)
        print(f"Written → {args.output}", file=sys.stderr)


if __name__ == "__main__":
    main()
