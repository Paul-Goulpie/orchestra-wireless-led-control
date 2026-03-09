# NRF24L01+PA+LNA — SPI Wiring for Orange Pi Zero 3

## Orange Pi Zero 3 GPIO header (26-pin)

The Orange Pi Zero 3 exposes SPI0 on its 26-pin GPIO header.
All signals are **3.3 V logic**; the NRF24L01+ module also runs at 3.3 V.

> **Warning:** The NRF24L01+PA+LNA module has a separate VCC pin that can
> accept 3.3 V or 5 V (check your specific module). Use a **3.3 V** supply
> for the logic lines.

---

## Pin mapping

| NRF24L01+ pin | Signal | OPi Zero 3 header pin | OPi GPIO (BCM-style) | Notes |
|:---:|---|:---:|---|---|
| 1 | GND   | 6  | GND       | Ground |
| 2 | VCC   | 1  | 3.3 V     | 3.3 V supply |
| 3 | CE    | 22 | **PA0 / GPIO 0** | Configurable — see below |
| 4 | CSN   | 24 | SPI0_CS0  | Chip Select (hardware SPI) |
| 5 | SCK   | 23 | SPI0_CLK  | SPI clock |
| 6 | MOSI  | 19 | SPI0_MOSI | SPI data out |
| 7 | MISO  | 21 | SPI0_MISO | SPI data in |
| 8 | IRQ   | —  | —         | Not used (polling mode) |

> CE pin: PA0 corresponds to GPIO number **0** in the sysfs/libgpiod
> numbering on the OPi Zero 3. Update `ce_pin` in `orchgateway.json`.
> You can use any free GPIO — just pick a convenient one and update the
> config accordingly.

---

## SPI device

The SPI bus appears as `/dev/spidev0.0` once the SPI overlay is enabled.

### Enable SPI on Orange Pi Zero 3 (Debian / armbian)

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

## Default orchgateway.json radio section

```json
"radio": {
  "spi_device":   "/dev/spidev0.0",
  "spi_speed_hz": 10000000,
  "ce_pin":       0,
  "channel":      76,
  "data_rate":    "1mbps",
  "repeat_count": 1
}
```

---

## SPI bus speed

The NRF24L01+ supports SPI clock up to **10 MHz**. The default of 10 MHz
works reliably; lower it to 2–4 MHz if you experience wiring issues.

---

## Raspberry Pi (reference wiring)

If using a Raspberry Pi instead of Orange Pi Zero 3:

| NRF24L01+ pin | Signal | RPi header pin | BCM GPIO |
|:---:|---|:---:|:---:|
| 1 | GND   | 20 | GND  |
| 2 | VCC   |  1 | 3.3 V |
| 3 | CE    | 22 | **25** |
| 4 | CSN   | 24 | 8 (CE0) |
| 5 | SCK   | 23 | 11 |
| 6 | MOSI  | 19 | 10 |
| 7 | MISO  | 21 | 9 |

CE = GPIO 25 (BCM) → set `"ce_pin": 25` in config (RPi default).

---

## Decoupling capacitor

Place a **10 µF** electrolytic capacitor (and optionally a 100 nF ceramic)
between VCC and GND as close to the NRF24L01+ module as possible.
The PA+LNA variant draws significant current on TX bursts; without decoupling
the radio may reset or malfunction.
