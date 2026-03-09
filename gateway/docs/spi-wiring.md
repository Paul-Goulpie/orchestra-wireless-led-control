# NRF24L01+PA+LNA — SPI Wiring

> **Warning:** The NRF24L01+PA+LNA module has a separate VCC pin that can
> accept 3.3 V or 5 V (check your specific module). Use a **3.3 V** supply
> for the logic lines.

---

## Orange Pi Zero 3 (default target)

The Orange Pi Zero 3 exposes SPI0 on its 26-pin GPIO header.
All signals are **3.3 V logic**.

### Pin mapping

| NRF24L01+ pin | Signal | OPi header pin | OPi GPIO | Notes |
|:---:|---|:---:|---|---|
| 1 | GND  |  6 | GND           | Ground |
| 2 | VCC  |  1 | 3.3 V         | 3.3 V supply |
| 3 | CE   | 22 | **PA0 / GPIO 0**  | Chip Enable |
| 4 | CSN  | 24 | SPI0_CS0      | Chip Select (hardware SPI) |
| 5 | SCK  | 23 | SPI0_CLK      | SPI clock |
| 6 | MOSI | 19 | SPI0_MOSI     | SPI data out |
| 7 | MISO | 21 | SPI0_MISO     | SPI data in |
| 8 | IRQ  | 18 | **PC6 / GPIO 70** | Interrupt — active LOW |

### Default config values (OPi Zero 3)

```json
"radio": {
  "spi_device":   "/dev/spidev0.0",
  "spi_speed_hz": 10000000,
  "ce_pin":       0,
  "channel":      76,
  "data_rate":    "1mbps",
  "repeat_count": 1,
  "irq_pin":      70
}
```

### Enable SPI (Debian / Armbian)

```bash
# Add to /boot/armbianEnv.txt (or /boot/orangepiEnv.txt):
overlays=spi-spidev
param_spidev_spi_bus=0
param_spidev_spi_cs=0

# Rebuild initramfs and reboot:
sudo update-initramfs -u
sudo reboot
```

Verify:

```bash
ls -l /dev/spidev0.0
# crw-rw---- 1 root spi 153, 0 ...
```

---

## Raspberry Pi (reference)

| NRF24L01+ pin | Signal | RPi header pin | BCM GPIO | Notes |
|:---:|---|:---:|:---:|---|
| 1 | GND  | 20 | GND      | Ground |
| 2 | VCC  |  1 | 3.3 V    | 3.3 V supply |
| 3 | CE   | 22 | **25**   | Chip Enable |
| 4 | CSN  | 24 | 8 (CE0)  | Chip Select (hardware SPI) |
| 5 | SCK  | 23 | 11       | SPI clock |
| 6 | MOSI | 19 | 10       | SPI data out |
| 7 | MISO | 21 | 9        | SPI data in |
| 8 | IRQ  | 18 | **24**   | Interrupt — active LOW |

### Config values (RPi)

```json
"radio": {
  "spi_device":   "/dev/spidev0.0",
  "spi_speed_hz": 10000000,
  "ce_pin":       25,
  "channel":      76,
  "data_rate":    "1mbps",
  "repeat_count": 1,
  "irq_pin":      24
}
```

---

## IRQ pin — interrupt-driven TX

The NRF24L01+ IRQ pin (pin 8) is active LOW and asserts after a successful
transmission (TX_DS). `orchgateway` uses it to avoid busy-polling the STATUS
register over SPI after each packet:

- `maskIRQ(tx_ok=false, tx_fail=true, rx_ready=true)` — only TX_DS asserts the pin
- After `writeFast()`, the gateway does a `poll(POLLPRI, 5 ms)` on the GPIO sysfs
  value file and calls `whatHappened()` to clear the flag
- On timeout (no IRQ within 5 ms) the TX FIFO is flushed and the send is retried
  on the next cycle

To **disable** IRQ (blocking TX fallback): set `"irq_pin": -1`.

---

## SPI bus speed

The NRF24L01+ supports SPI clock up to **10 MHz**. Lower to 2–4 MHz if you
experience wiring issues.

---

## Decoupling capacitor

Place a **10 µF** electrolytic capacitor (and optionally a 100 nF ceramic)
between VCC and GND as close to the NRF24L01+ module as possible.
The PA+LNA variant draws significant current on TX bursts; without decoupling
the radio may reset or malfunction.
