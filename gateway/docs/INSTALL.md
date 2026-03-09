# orchgateway — Build & Installation

## Target

Orange Pi Zero 3 running Debian/Armbian (aarch64), or any Linux host with SPI.
Can also be compiled natively on Raspberry Pi OS.

---

## Dependencies

### 1. Build tools

```bash
sudo apt-get install build-essential xxd
```

### 2. cJSON

```bash
sudo apt-get install libcjson-dev
```

### 3. RF24 library

The RF24 library for Linux is not packaged in Debian; build it from source.

```bash
# Install prerequisites
sudo apt-get install cmake git

# Clone and build
git clone https://github.com/nRF24/RF24.git
cd RF24
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install
sudo ldconfig
```

---

## Build (native on target)

```bash
cd gateway
make
```

Binary is placed in `gateway/build/orchgateway`.

---

## Cross-compile (from x86 host to aarch64)

```bash
sudo apt-get install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu

# Build RF24 for target (install sysroot with RF24 headers + lib on host)
# ... or copy headers/libs from the target into a sysroot

cd gateway
make CROSS_PREFIX=aarch64-linux-gnu-
```

Copy `build/orchgateway` to the Orange Pi Zero 3.

---

## Install

```bash
sudo make install
```

This installs:
- `/usr/local/bin/orchgateway`
- `/etc/orchgateway.json` (default config, if not already present)
- `/etc/systemd/system/orchgateway.service`

---

## Autostart with systemd

```bash
sudo systemctl enable --now orchgateway
sudo systemctl status orchgateway
sudo journalctl -u orchgateway -f
```

---

## SPI / GPIO access

By default the service runs as root to access `/dev/spidev0.0` and GPIO.

To run as a dedicated user, create udev rules and add the user to the
`spi` and `gpio` groups:

```bash
sudo usermod -aG spi,gpio <username>
```

See `docs/spi-wiring.md` for SPI wiring and overlay configuration.
