#ifndef STATS_H
#define STATS_H

#include <stdint.h>
#include <time.h>

typedef struct {
    /* sACN / network */
    uint64_t sacn_rx;             /* sACN packets received */
    uint64_t sacn_skipped;        /* sACN packets skipped (no matching node) */
    uint64_t net_timeout_count;   /* number of sACN timeout events */

    /* Radio TX */
    uint64_t radio_tx_total;      /* total radio packets transmitted */
    uint64_t radio_tx_failed;     /* radio TX failures */
    uint64_t radio_refresh_total; /* refresh packets sent */

    time_t start_time;
} stats_t;

extern stats_t g_stats;

void stats_init(void);
void stats_print(void);

#endif /* STATS_H */
