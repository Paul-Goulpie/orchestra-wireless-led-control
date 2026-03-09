#include "stats.h"
#include "log.h"
#include <string.h>
#include <inttypes.h>

stats_t g_stats;

void stats_init(void)
{
    memset(&g_stats, 0, sizeof(g_stats));
    g_stats.start_time = time(NULL);
}

void stats_print(void)
{
    time_t uptime = time(NULL) - g_stats.start_time;
    int h = (int)(uptime / 3600);
    int m = (int)((uptime % 3600) / 60);
    int s = (int)(uptime % 60);

    LOG_INFO("=== Statistics (uptime %02d:%02d:%02d) ===", h, m, s);
    LOG_INFO("  sACN rx            : %" PRIu64, g_stats.sacn_rx);
    LOG_INFO("  sACN skipped       : %" PRIu64, g_stats.sacn_skipped);
    LOG_INFO("  Network timeouts   : %" PRIu64, g_stats.net_timeout_count);
    LOG_INFO("  Radio TX total     : %" PRIu64, g_stats.radio_tx_total);
    LOG_INFO("  Radio TX failures  : %" PRIu64, g_stats.radio_tx_failed);
    LOG_INFO("  Radio refresh sent : %" PRIu64, g_stats.radio_refresh_total);
    LOG_INFO("=========================================");
}
