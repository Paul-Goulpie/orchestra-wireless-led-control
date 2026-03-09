/*
 * radio.cpp — C++ wrapper around the RF24 Linux library.
 *
 * TX modes:
 *   IRQ-driven (irq_pin >= 0): writeFast() + poll() on GPIO sysfs falling edge.
 *     The IRQ pin (active LOW) is asserted by the NRF24L01+ when TX_DS fires,
 *     i.e. after the packet has been sent over the air.
 *     MAX_RT and RX_DR are masked on the IRQ pin (auto-ACK disabled → MAX_RT
 *     never fires; we are TX-only → RX_DR irrelevant).
 *
 *   Blocking fallback (irq_pin = -1): RF24::write() polls the STATUS register.
 *
 * Library: https://github.com/nRF24/RF24
 * Install: see gateway/docs/INSTALL.md
 */

#include "radio.h"
#include "node.h"
#include "log.h"

#include <RF24/RF24.h>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>

/* Broadcast pipe address — must match firmware (pipe address 0xE8E8F0F0E1,
 * stored LSByte-first as required by the library). */
static const uint8_t PIPE_ADDR[5] = { 0xE1, 0xF0, 0xF0, 0xE8, 0xE8 };

/* IRQ TX timeout: at 1 Mbps a 32-byte packet takes ~350 µs.
 * 5 ms is very conservative but keeps us responsive. */
#define IRQ_TX_TIMEOUT_MS 5

static RF24 *g_radio   = nullptr;
static int   g_repeats = 1;
static int   g_irq_fd  = -1;   /* sysfs value fd, -1 = IRQ not used */
static int   g_irq_pin = -1;   /* BCM GPIO number */

/* -----------------------------------------------------------------------
 * GPIO sysfs helpers
 * --------------------------------------------------------------------- */

static int irq_gpio_setup(int pin)
{
    char path[64];
    char buf[16];
    int  n, fd;

    /* Export (ignore EBUSY — already exported from a previous run) */
    fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd >= 0) {
        n = snprintf(buf, sizeof(buf), "%d", pin);
        write(fd, buf, n);   /* EBUSY is expected if already exported */
        close(fd);
        usleep(100000);      /* let udev create the sysfs files */
    }

    /* direction = in */
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", pin);
    fd = open(path, O_WRONLY);
    if (fd < 0) {
        LOG_ERROR("IRQ GPIO %d: open direction: %s", pin, strerror(errno));
        return -1;
    }
    write(fd, "in", 2);
    close(fd);

    /* edge = falling  (IRQ is active LOW → asserted on falling edge) */
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/edge", pin);
    fd = open(path, O_WRONLY);
    if (fd < 0) {
        LOG_ERROR("IRQ GPIO %d: open edge: %s", pin, strerror(errno));
        return -1;
    }
    write(fd, "falling", 7);
    close(fd);

    /* Open value file for poll() */
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", pin);
    fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        LOG_ERROR("IRQ GPIO %d: open value: %s", pin, strerror(errno));
        return -1;
    }

    /* Initial read clears any stale pending interrupt */
    char dummy[4];
    read(fd, dummy, sizeof(dummy));

    return fd;
}

static void irq_gpio_unexport(int pin)
{
    int fd = open("/sys/class/gpio/unexport", O_WRONLY);
    if (fd >= 0) {
        char buf[16];
        int n = snprintf(buf, sizeof(buf), "%d", pin);
        write(fd, buf, n);
        close(fd);
    }
}

/* -----------------------------------------------------------------------
 * IRQ-driven TX wait
 *
 * Must be called immediately after writeFast().
 * poll() blocks until the IRQ pin goes LOW (TX_DS asserted).
 * whatHappened() reads and clears the STATUS flags, which de-asserts IRQ.
 * --------------------------------------------------------------------- */

static bool wait_for_tx_irq(void)
{
    /* Drain any residual value so poll() detects only the next edge */
    char dummy[4];
    lseek(g_irq_fd, 0, SEEK_SET);
    read(g_irq_fd, dummy, sizeof(dummy));

    struct pollfd pfd = { .fd = g_irq_fd, .events = POLLPRI | POLLERR };
    int ret = poll(&pfd, 1, IRQ_TX_TIMEOUT_MS);

    /* Always clear IRQ flags via SPI regardless of poll outcome */
    bool tx_ok, tx_fail, rx_ready;
    g_radio->whatHappened(tx_ok, tx_fail, rx_ready);

    if (ret <= 0) {
        LOG_WARN("Radio: IRQ TX timeout (%d ms) — flushing TX FIFO", IRQ_TX_TIMEOUT_MS);
        g_radio->flush_tx();
        return false;
    }

    if (tx_fail) {
        /* Should never happen with auto-ACK disabled */
        g_radio->flush_tx();
        return false;
    }

    return tx_ok;
}

/* -----------------------------------------------------------------------
 * Public C interface
 * --------------------------------------------------------------------- */

extern "C" {

int radio_init(const radio_config_t *cfg)
{
    if (!cfg) return -1;

    g_repeats = (cfg->repeat_count >= 0) ? cfg->repeat_count : 0;

    /* Derive SPI CSN index from device path: "/dev/spidev0.0" → 0 */
    uint8_t csn = 0;
    const char *dot = strrchr(cfg->spi_device, '.');
    if (dot && *(dot + 1) != '\0')
        csn = (uint8_t)(*(dot + 1) - '0');

    g_radio = new RF24(cfg->ce_pin, csn, cfg->spi_speed_hz);

    if (!g_radio->begin()) {
        LOG_ERROR("RF24::begin() failed — check wiring, SPI device %s, CE=%u",
                  cfg->spi_device, cfg->ce_pin);
        delete g_radio;
        g_radio = nullptr;
        return -1;
    }

    g_radio->setChannel(cfg->channel);

    switch (cfg->data_rate) {
    case RADIO_RATE_250KBPS: g_radio->setDataRate(RF24_250KBPS); break;
    case RADIO_RATE_2MBPS:   g_radio->setDataRate(RF24_2MBPS);   break;
    default:
    case RADIO_RATE_1MBPS:   g_radio->setDataRate(RF24_1MBPS);   break;
    }

    g_radio->setPALevel(RF24_PA_MAX);
    g_radio->setAutoAck(false);
    g_radio->setPayloadSize(NODE_PACKET_SIZE);
    g_radio->openWritingPipe(PIPE_ADDR);
    g_radio->stopListening();

    /* ---- IRQ pin setup ---- */
    if (cfg->irq_pin >= 0) {
        /*
         * Mask MAX_RT and RX_DR on the IRQ pin — only TX_DS will assert it.
         * maskIRQ(tx_ok=false, tx_fail=true, rx_ready=true):
         *   false → TX_DS  NOT masked → asserts IRQ on successful TX
         *   true  → MAX_RT masked    → never fires (no auto-ACK)
         *   true  → RX_DR  masked    → we are TX-only
         */
        g_radio->maskIRQ(false, true, true);

        g_irq_fd = irq_gpio_setup(cfg->irq_pin);
        if (g_irq_fd < 0) {
            LOG_WARN("IRQ GPIO %d setup failed — falling back to blocking TX",
                     cfg->irq_pin);
        } else {
            g_irq_pin = cfg->irq_pin;
            LOG_INFO("IRQ-driven TX enabled on GPIO %d", cfg->irq_pin);
        }
    }

    const char *rate_str = (cfg->data_rate == RADIO_RATE_250KBPS) ? "250kbps" :
                           (cfg->data_rate == RADIO_RATE_2MBPS)   ? "2mbps"   : "1mbps";

    LOG_INFO("Radio OK — chip=%s channel=%u rate=%s CE=%u IRQ=%s SPI=%s repeat=%d",
             g_radio->isPVariant() ? "nRF24L01+" : "nRF24L01",
             cfg->channel, rate_str, cfg->ce_pin,
             g_irq_fd >= 0 ? "enabled" : "disabled (blocking)",
             cfg->spi_device, g_repeats);

    return 0;
}

int radio_send(const void *data, uint8_t len)
{
    if (!g_radio || !data) return -1;
    if (len > 32) len = 32;

    bool ok = true;

    for (int i = 0; i <= g_repeats; i++) {
        if (g_irq_fd >= 0) {
            /* IRQ-driven: non-blocking load + GPIO interrupt wait */
            g_radio->writeFast(data, len);
            if (!wait_for_tx_irq())
                ok = false;
        } else {
            /* Blocking fallback: RF24 polls STATUS register internally */
            if (!g_radio->write(data, len))
                ok = false;
        }
    }

    return ok ? 0 : -1;
}

void radio_close(void)
{
    if (g_irq_fd >= 0) {
        close(g_irq_fd);
        g_irq_fd = -1;
        irq_gpio_unexport(g_irq_pin);
        g_irq_pin = -1;
    }
    delete g_radio;
    g_radio = nullptr;
}

const char *radio_chip_model(void)
{
    if (!g_radio) return "unknown";
    return g_radio->isPVariant() ? "nRF24L01+" : "nRF24L01";
}

} /* extern "C" */
