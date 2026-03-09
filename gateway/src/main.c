/*
 * orchgateway — Orchestra Wireless LED Gateway
 *
 * Receives sACN (E1.31) DMX frames from QLC+ via the ETC Labs sACN library
 * and forwards them as NRF24L01+ radio packets to up to 60 wearable LED nodes.
 *
 * The sACN library manages its own receiver thread. Node state is protected
 * by a mutex shared between the sACN callback thread and the main loop.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>

#include "config.h"
#include "sacn.h"
#include "radio.h"
#include "node.h"
#include "stats.h"
#include "log.h"

#define APP_NAME    "orchgateway"
#define APP_VERSION "1.0.0"
#define DEFAULT_CFG "/etc/orchgateway.json"

/* -----------------------------------------------------------------------
 * Shared state between sACN callback thread and main thread
 * --------------------------------------------------------------------- */

typedef struct {
    app_config_t   *cfg;
    pthread_mutex_t mutex;
    int64_t         last_sacn_ms;  /* monotonic ms of last received data */
    int             sacn_active;   /* 1 = at least one packet received */
    int             timed_out;     /* 1 = blackout already sent */
} gw_ctx_t;

/* -----------------------------------------------------------------------
 * Monotonic clock helper
 * --------------------------------------------------------------------- */

static int64_t mono_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* -----------------------------------------------------------------------
 * sACN callbacks  (called from the sACN library's internal thread)
 * --------------------------------------------------------------------- */

static void on_universe_data(uint16_t        universe_id,
                             const uint8_t  *dmx,
                             uint16_t        slot_start,
                             uint16_t        slot_count,
                             void           *ctx)
{
    gw_ctx_t *gw = (gw_ctx_t *)ctx;

    pthread_mutex_lock(&gw->mutex);

    gw->last_sacn_ms = mono_ms();
    gw->sacn_active  = 1;
    gw->timed_out    = 0;
    g_stats.sacn_rx++;

    int matched = 0;
    for (int i = 0; i < gw->cfg->num_nodes; i++) {
        node_state_t *nd = &gw->cfg->nodes[i];
        if (nd->universe_id != universe_id) continue;
        matched++;
        if (node_apply_dmx(nd, dmx, slot_start, slot_count))
            nd->dirty = true;
    }
    if (!matched)
        g_stats.sacn_skipped++;

    pthread_mutex_unlock(&gw->mutex);
}

static void on_source_lost(uint16_t universe_id, void *ctx)
{
    /* The main loop handles the actual blackout via timeout detection.
     * Just update stats here; the log is already printed by sacn.c. */
    (void)universe_id;
    (void)ctx;
    g_stats.net_timeout_count++;
}

/* -----------------------------------------------------------------------
 * Signal handling
 * --------------------------------------------------------------------- */

static volatile int g_running = 1;

static void sig_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

/* -----------------------------------------------------------------------
 * Help
 * --------------------------------------------------------------------- */

static void print_help(const char *prog)
{
    printf(
        "Usage: %s [OPTIONS]\n"
        "\n"
        "Orchestra Wireless LED Gateway v" APP_VERSION "\n"
        "Converts sACN (E1.31) DMX frames to NRF24L01+ radio packets.\n"
        "\n"
        "Options:\n"
        "  %-32s %s\n"
        "  %-32s %s\n"
        "  %-32s %s\n"
        "  %-32s %s (default: " DEFAULT_CFG ")\n"
        "  %-32s %s (default: 76)\n"
        "  %-32s %s (default: 1mbps)\n"
        "  %-32s %s (default: 1)\n"
        "  %-32s %s (default: 2000)\n"
        "  %-32s %s (default: 30)\n"
        "\n"
        "Data rates: 250kbps | 1mbps | 2mbps\n"
        "\n"
        "Configuration file: JSON (created with built-in defaults if absent).\n"
        "\n"
        "Examples:\n"
        "  %s -v -f /tmp/myconf.json\n"
        "  %s --channel 100 --repeat-count 2\n",
        prog,
        "-h, --help",            "Show this help and exit",
        "-V, --version",         "Show version and exit",
        "-v, --verbose",         "Enable debug output",
        "-f, --config FILE",     "Configuration file",
        "-c, --channel N",       "RF channel override (0-125)",
        "-R, --data-rate RATE",  "RF data rate override",
        "-r, --repeat-count N",  "Extra TX repetitions per packet",
        "-t, --sacn-timeout MS", "sACN silence timeout in ms",
        "-s, --stats-interval S","Statistics print interval (seconds)",
        prog, prog);
}

/* -----------------------------------------------------------------------
 * main
 * --------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    const char *config_path = DEFAULT_CFG;
    int         verbose     = 0;
    int         stats_ivl   = 30;
    int         ov_channel  = -1;
    int         ov_repeat   = -1;
    int         ov_timeout  = -1;
    const char *ov_rate     = NULL;

    static const struct option long_opts[] = {
        { "help",           no_argument,       NULL, 'h' },
        { "version",        no_argument,       NULL, 'V' },
        { "verbose",        no_argument,       NULL, 'v' },
        { "config",         required_argument, NULL, 'f' },
        { "channel",        required_argument, NULL, 'c' },
        { "data-rate",      required_argument, NULL, 'R' },
        { "repeat-count",   required_argument, NULL, 'r' },
        { "sacn-timeout",   required_argument, NULL, 't' },
        { "stats-interval", required_argument, NULL, 's' },
        { NULL, 0, NULL, 0 }
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "hVvf:c:R:r:t:s:", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'h': print_help(argv[0]); return 0;
        case 'V': printf("%s %s\n", APP_NAME, APP_VERSION); return 0;
        case 'v': verbose     = 1;       break;
        case 'f': config_path = optarg;  break;
        case 'c': ov_channel  = atoi(optarg); break;
        case 'R': ov_rate     = optarg;  break;
        case 'r': ov_repeat   = atoi(optarg); break;
        case 't': ov_timeout  = atoi(optarg); break;
        case 's': stats_ivl   = atoi(optarg); break;
        default:  print_help(argv[0]); return 1;
        }
    }

    log_init(verbose);
    stats_init();
    LOG_INFO("%s v%s starting", APP_NAME, APP_VERSION);

    /* ---- Load configuration ---- */
    app_config_t cfg;
    if (config_load(config_path, &cfg) != 0) {
        LOG_ERROR("Failed to load config from '%s'", config_path);
        return 1;
    }

    if (ov_channel >= 0 && ov_channel <= 125) cfg.radio.channel      = (uint8_t)ov_channel;
    if (ov_repeat  >= 0)                       cfg.radio.repeat_count = ov_repeat;
    if (ov_timeout >= 0)                       cfg.sacn_timeout_ms    = (uint32_t)ov_timeout;
    if (ov_rate) {
        if      (strcmp(ov_rate, "250kbps") == 0) cfg.radio.data_rate = RADIO_RATE_250KBPS;
        else if (strcmp(ov_rate, "2mbps")   == 0) cfg.radio.data_rate = RADIO_RATE_2MBPS;
        else                                       cfg.radio.data_rate = RADIO_RATE_1MBPS;
    }

    config_print(&cfg);

    if (cfg.num_universes == 0) { LOG_ERROR("No universes configured"); return 1; }
    if (cfg.num_nodes     == 0) { LOG_WARN("No nodes configured"); }

    /* ---- Shared gateway context (callback ↔ main loop) ---- */
    gw_ctx_t gw = {0};
    gw.cfg          = &cfg;
    gw.last_sacn_ms = mono_ms();
    pthread_mutex_init(&gw.mutex, NULL);

    /* ---- Initialize sACN library ---- */
    if (sacn_recv_init() != 0) {
        LOG_ERROR("sACN initialization failed");
        return 1;
    }

    /* Create one receiver per universe */
    for (int i = 0; i < cfg.num_universes; i++) {
        if (sacn_recv_add_universe(cfg.universes[i].id,
                                   on_universe_data,
                                   on_source_lost,
                                   &gw) != 0) {
            LOG_ERROR("Failed to create receiver for universe %u", cfg.universes[i].id);
        }
    }

    /* ---- Initialize radio ---- */
    if (radio_init(&cfg.radio) != 0) {
        LOG_ERROR("Radio initialization failed");
        sacn_recv_deinit();
        return 1;
    }

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    LOG_INFO("Gateway running — %d node(s), %d universe(s). Ctrl-C to stop.",
             cfg.num_nodes, cfg.num_universes);

    /* ---- Main loop ---- */
    int64_t last_refresh_ms = mono_ms();
    int64_t last_stats_ms   = mono_ms();
    int64_t stats_ivl_ms    = (int64_t)stats_ivl * 1000;

    /* Packets to send, collected under lock then transmitted outside */
    node_packet_v1_t tx_queue[CONFIG_MAX_NODES];
    int              tx_count = 0;

    while (g_running) {

        /* Sleep 50 ms between iterations (sACN lib threads handle reception) */
        struct timespec sleep_ts = { .tv_sec = 0, .tv_nsec = 50000000L };
        nanosleep(&sleep_ts, NULL);

        int64_t now = mono_ms();

        /* ---- Collect dirty nodes and check timeout (under lock) ---- */
        tx_count = 0;
        int send_blackout = 0;

        pthread_mutex_lock(&gw.mutex);

        int64_t sacn_age_ms = now - gw.last_sacn_ms;

        if (gw.sacn_active &&
            !gw.timed_out  &&
            sacn_age_ms > (int64_t)cfg.sacn_timeout_ms) {
            LOG_WARN("sACN timeout after %.1f s — sending blackout",
                     (double)sacn_age_ms / 1000.0);
            gw.timed_out = 1;
            send_blackout = 1;
            /* Reset node state */
            for (int i = 0; i < cfg.num_nodes; i++) {
                memset(cfg.nodes[i].rgb, 0, NODE_CHANNELS);
                cfg.nodes[i].initialized = false;
                cfg.nodes[i].dirty       = false;
            }
        }

        if (!send_blackout) {
            for (int i = 0; i < cfg.num_nodes; i++) {
                node_state_t *nd = &cfg.nodes[i];
                if (nd->dirty) {
                    node_build_packet(nd, &tx_queue[tx_count++]);
                    nd->dirty = false;
                }
            }
        }

        pthread_mutex_unlock(&gw.mutex);

        /* ---- Transmit (outside lock to minimise contention) ---- */
        if (send_blackout) {
            for (int i = 0; i < cfg.num_nodes; i++) {
                node_packet_v1_t pkt;
                node_build_off_packet(&cfg.nodes[i], &pkt);
                if (radio_send(&pkt, sizeof(pkt)) == 0)
                    g_stats.radio_tx_total++;
                else {
                    g_stats.radio_tx_total++;
                    g_stats.radio_tx_failed++;
                    LOG_WARN("TX blackout failed: node %u", cfg.nodes[i].address);
                }
            }
        } else {
            for (int i = 0; i < tx_count; i++) {
                int ret = radio_send(&tx_queue[i], sizeof(tx_queue[i]));
                g_stats.radio_tx_total++;
                if (ret != 0) {
                    g_stats.radio_tx_failed++;
                    LOG_WARN("TX failed: node %u", tx_queue[i].dst_addr);
                } else {
                    LOG_DEBUG("TX node %u frame_id=%u",
                              tx_queue[i].dst_addr, tx_queue[i].frame_id);
                }
            }
        }

        /* ---- Periodic refresh ---- */
        if ((now - last_refresh_ms) >= (int64_t)cfg.refresh_interval_ms) {
            last_refresh_ms = now;

            pthread_mutex_lock(&gw.mutex);
            int refresh_count = 0;
            node_packet_v1_t refresh_queue[CONFIG_MAX_NODES];
            if (!gw.timed_out) {
                for (int i = 0; i < cfg.num_nodes; i++) {
                    node_state_t *nd = &cfg.nodes[i];
                    if (!nd->dirty && nd->initialized)
                        node_build_packet(nd, &refresh_queue[refresh_count++]);
                }
            }
            pthread_mutex_unlock(&gw.mutex);

            for (int i = 0; i < refresh_count; i++) {
                int ret = radio_send(&refresh_queue[i], sizeof(refresh_queue[i]));
                g_stats.radio_tx_total++;
                g_stats.radio_refresh_total++;
                if (ret != 0)
                    g_stats.radio_tx_failed++;
            }
        }

        /* ---- Statistics ---- */
        if ((now - last_stats_ms) >= stats_ivl_ms) {
            last_stats_ms = now;
            stats_print();
        }
    }

    /* ---- Graceful shutdown ---- */
    LOG_INFO("Shutting down — sending blackout...");
    for (int i = 0; i < cfg.num_nodes; i++) {
        node_packet_v1_t pkt;
        node_build_off_packet(&cfg.nodes[i], &pkt);
        radio_send(&pkt, sizeof(pkt));
    }

    sacn_recv_deinit();
    radio_close();
    pthread_mutex_destroy(&gw.mutex);

    stats_print();
    LOG_INFO("Goodbye.");
    return 0;
}
