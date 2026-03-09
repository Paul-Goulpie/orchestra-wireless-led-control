#ifndef RADIO_H
#define RADIO_H

#include <stdint.h>

/* Data rates supported by NRF24L01+ */
typedef enum {
    RADIO_RATE_250KBPS = 0,
    RADIO_RATE_1MBPS,
    RADIO_RATE_2MBPS
} radio_rate_t;

typedef struct {
    char         spi_device[64]; /* e.g. "/dev/spidev0.0" */
    uint32_t     spi_speed_hz;   /* SPI clock speed, e.g. 10000000 */
    uint8_t      ce_pin;         /* GPIO BCM pin for CE */
    int          irq_pin;        /* GPIO BCM pin for IRQ (-1 = disabled, use blocking TX) */
    uint8_t      channel;        /* RF channel 0-125 */
    radio_rate_t data_rate;
    int          repeat_count;   /* extra TX repetitions (0 = send once total) */
} radio_config_t;

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the radio. Returns 0 on success, -1 on failure. */
int radio_init(const radio_config_t *cfg);

/*
 * Transmit a 32-byte packet.
 * The packet is sent (1 + repeat_count) times.
 * Returns 0 on success, -1 on failure.
 */
int radio_send(const void *data, uint8_t len);

/* Release radio resources. */
void radio_close(void);

/* Return a human-readable chip model string. */
const char *radio_chip_model(void);

#ifdef __cplusplus
}
#endif

#endif /* RADIO_H */
