/*
 * radio.cpp — C++ wrapper around the RF24 Linux library.
 *
 * Exposes a pure-C interface (radio.h) so the rest of the application
 * can be written in plain C and linked with g++.
 *
 * Library: https://github.com/nRF24/RF24
 * Install: see gateway/docs/INSTALL.md
 */

#include "radio.h"
#include "node.h"
#include "log.h"

#include <RF24/RF24.h>
#include <cstring>
#include <cstdio>

/* Broadcast pipe address — must match firmware (pipe address 0xE8E8F0F0E1,
 * stored LSByte-first as required by the library). */
static const uint8_t PIPE_ADDR[5] = { 0xE1, 0xF0, 0xF0, 0xE8, 0xE8 };

static RF24 *g_radio    = nullptr;
static int   g_repeats  = 1;

/* Derive SPI CSN device index from spi_device path.
 * "/dev/spidev0.0" → 0, "/dev/spidev0.1" → 1, default → 0 */
static uint8_t csn_from_device(const char *dev)
{
    /* Look for the last '.' and parse the digit after it */
    const char *dot = strrchr(dev, '.');
    if (dot && *(dot + 1) != '\0')
        return (uint8_t)(*(dot + 1) - '0');
    return 0;
}

extern "C" {

int radio_init(const radio_config_t *cfg)
{
    if (!cfg) return -1;

    g_repeats = (cfg->repeat_count >= 0) ? cfg->repeat_count : 0;

    uint8_t csn = csn_from_device(cfg->spi_device);
    g_radio = new RF24(cfg->ce_pin, csn, cfg->spi_speed_hz);

    if (!g_radio->begin()) {
        LOG_ERROR("RF24::begin() failed — check wiring and SPI device %s (CE=%u)",
                  cfg->spi_device, cfg->ce_pin);
        delete g_radio;
        g_radio = nullptr;
        return -1;
    }

    g_radio->setChannel(cfg->channel);

    switch (cfg->data_rate) {
    case RADIO_RATE_250KBPS:
        g_radio->setDataRate(RF24_250KBPS);
        break;
    case RADIO_RATE_2MBPS:
        g_radio->setDataRate(RF24_2MBPS);
        break;
    default:
    case RADIO_RATE_1MBPS:
        g_radio->setDataRate(RF24_1MBPS);
        break;
    }

    g_radio->setPALevel(RF24_PA_MAX);
    g_radio->setAutoAck(false);
    g_radio->setPayloadSize(NODE_PACKET_SIZE);
    g_radio->openWritingPipe(PIPE_ADDR);
    g_radio->stopListening();

    const char *rate_str = (cfg->data_rate == RADIO_RATE_250KBPS) ? "250kbps" :
                           (cfg->data_rate == RADIO_RATE_2MBPS)   ? "2mbps"   : "1mbps";

    LOG_INFO("Radio OK — chip=%s channel=%u rate=%s CE=%u SPI=%s repeat=%d",
             g_radio->isPVariant() ? "nRF24L01+" : "nRF24L01",
             cfg->channel, rate_str, cfg->ce_pin,
             cfg->spi_device, g_repeats);

    return 0;
}

int radio_send(const void *data, uint8_t len)
{
    if (!g_radio || !data) return -1;
    if (len > 32) len = 32;

    bool ok = g_radio->write(data, len);

    for (int i = 0; i < g_repeats; i++)
        g_radio->write(data, len);

    return ok ? 0 : -1;
}

void radio_close(void)
{
    delete g_radio;
    g_radio = nullptr;
}

const char *radio_chip_model(void)
{
    if (!g_radio) return "unknown";
    return g_radio->isPVariant() ? "nRF24L01+" : "nRF24L01";
}

} /* extern "C" */
