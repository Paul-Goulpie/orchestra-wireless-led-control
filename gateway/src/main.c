/*
 * orchgateway — Orchestra Wireless LED Gateway
 *
 * Receives sACN (E1.31) DMX frames from QLC+ via the ETC Labs sACN library
 * and forwards them as NRF24L01+ radio packets to up to 60 wearable LED nodes.
 *
 * Threading model:
 *   - sACN library thread  : receives UDP, calls on_universe_data / on_source_lost
 *   - main thread          : waits on pthread_cond_t, transmits radio packets
 *
 * The condition variable is signalled by the sACN callbacks so the main thread
 * wakes up immediately when new data is available — no polling, no fixed sleep.
 *
 * Source-loss timeout is delegated entirely to the sACN library via
 * sacn_receiver_set_expired_wait(); on_source_lost() handles the blackout.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <signal.h>
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
    pthread_cond_t  cond;     /* signalled on new data or source loss */
    int             blackout; /* set by on_source_lost, cleared after TX */
} gw_ctx_t;

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

    pthread_cond_signal(&gw->cond); /* wake main thread immediately */
    pthread_mutex_unlock(&gw->mutex);
}

static void on_source_lost(uint16_t universe_id, void *ctx)
{
    gw_ctx_t *gw = (gw_ctx_t *)ctx;

    pthread_mutex_lock(&gw->mutex);

    LOG_WARN("Source lost on universe %u — blackout", universe_id);
    g_stats.net_timeout_count++;

    for (int i = 0; i < gw->cfg->num_nodes; i++) {
        node_state_t *nd = &gw->cfg->nodes[i];
        if (nd->universe_id != universe_id) continue;
        memset(nd->rgb, 0, NODE_CHANNELS);
        nd->initialized = false;
        nd->dirty       = false;
    }
    gw->blackout = 1;

    pthread_cond_signal(&gw->cond);
    pthread_mutex_unlock(&gw->mutex);
}

/* -----------------------------------------------------------------------
 * Signal handling
 * --------------------------------------------------------------------- */

static volatile int  g_running = 1;
static pthread_cond_t *g_shutdown_cond = NULL; /* to interrupt cond_timedwait */
static pthread_mutex_t *g_shutdown_mutex = NULL;

static void sig_handler(int sig)
{
    (void)sig;
    g_running = 0;
    /* Wake the main thread if it is blocked in cond_timedwait */
    if (g_shutdown_cond && g_shutdown_mutex) {
        pthread_mutex_lock(g_shutdown_mutex);
        pthread_cond_signal(g_shutdown_cond);
        pthread_mutex_unlock(g_shutdown_mutex);
    }
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
        "  %s --dry-run -v            # validate sACN reception without radio\n"
        "  %s --channel 100 --repeat-count 2\n",
        prog,
        "-h, --help",            "Show this help and exit",
        "-V, --version",         "Show version and exit",
        "-v, --verbose",         "Enable debug output",
        "-n, --dry-run",         "Disable radio — log TX instead (sACN validation)",
        "-f, --config FILE",     "Configuration file",
        "-c, --channel N",       "RF channel override (0-125)",
        "-R, --data-rate RATE",  "RF data rate override",
        "-r, --repeat-count N",  "Extra TX repetitions per packet",
        "-t, --sacn-timeout MS", "sACN source-loss timeout in ms",
        "-s, --stats-interval S","Statistics print interval (seconds)",
        prog, prog, prog);
}

/* -----------------------------------------------------------------------
 * Absolute timespec helper for pthread_cond_timedwait (CLOCK_MONOTONIC)
 * --------------------------------------------------------------------- */

static void make_abs_timeout(struct timespec *ts, uint32_t offset_ms)
{
    clock_gettime(CLOCK_MONOTONIC, ts);
    ts->tv_nsec += (long)offset_ms * 1000000L;
    ts->tv_sec  += ts->tv_nsec / 1000000000L;
    ts->tv_nsec %= 1000000000L;
}

/* -----------------------------------------------------------------------
 * main
 * --------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    const char *config_path = DEFAULT_CFG;
    int         verbose     = 0;
    int         dry_run     = 0;
    int         stats_ivl   = 30;
    int         ov_channel  = -1;
    int         ov_repeat   = -1;
    int         ov_timeout  = -1;
    const char *ov_rate     = NULL;

    static const struct option long_opts[] = {
        { "help",           no_argument,       NULL, 'h' },
        { "version",        no_argument,       NULL, 'V' },
        { "verbose",        no_argument,       NULL, 'v' },
        { "dry-run",        no_argument,       NULL, 'n' },
        { "config",         required_argument, NULL, 'f' },
        { "channel",        required_argument, NULL, 'c' },
        { "data-rate",      required_argument, NULL, 'R' },
        { "repeat-count",   required_argument, NULL, 'r' },
        { "sacn-timeout",   required_argument, NULL, 't' },
        { "stats-interval", required_argument, NULL, 's' },
        { NULL, 0, NULL, 0 }
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "hVvnf:c:R:r:t:s:", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'h': print_help(argv[0]); return 0;
        case 'V': printf("%s %s\n", APP_NAME, APP_VERSION); return 0;
        case 'v': verbose     = 1;            break;
        case 'n': dry_run     = 1;            break;
        case 'f': config_path = optarg;       break;
        case 'c': ov_channel  = atoi(optarg); break;
        case 'R': ov_rate     = optarg;       break;
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

    /* ---- Gateway context ---- */
    gw_ctx_t gw = {0};
    gw.cfg = &cfg;

    pthread_mutex_init(&gw.mutex, NULL);

    /* Use CLOCK_MONOTONIC for the condition variable */
    pthread_condattr_t cattr;
    pthread_condattr_init(&cattr);
    pthread_condattr_setclock(&cattr, CLOCK_MONOTONIC);
    pthread_cond_init(&gw.cond, &cattr);
    pthread_condattr_destroy(&cattr);

    /* Expose to signal handler */
    g_shutdown_cond  = &gw.cond;
    g_shutdown_mutex = &gw.mutex;

    /* ---- Initialize sACN library ---- */
    if (sacn_recv_init(cfg.sacn_timeout_ms) != 0) {
        LOG_ERROR("sACN initialization failed");
        return 1;
    }

    for (int i = 0; i < cfg.num_universes; i++) {
        if (sacn_recv_add_universe(cfg.universes[i].id,
                                   on_universe_data,
                                   on_source_lost,
                                   &gw) != 0) {
            LOG_ERROR("Failed to create receiver for universe %u",
                      cfg.universes[i].id);
        }
    }

    /* ---- Initialize radio ---- */
    if (dry_run) {
        LOG_INFO("*** DRY-RUN mode — radio disabled, TX will be logged only ***");
    } else if (radio_init(&cfg.radio) != 0) {
        LOG_ERROR("Radio initialization failed");
        sacn_recv_deinit();
        return 1;
    }

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    LOG_INFO("Gateway running — %d node(s), %d universe(s). Ctrl-C to stop.",
             cfg.num_nodes, cfg.num_universes);

    /* ---- Main loop ---- */
    /*
     * The main thread blocks on pthread_cond_timedwait():
     *   - Woken immediately by on_universe_data() when new DMX arrives
     *   - Woken immediately by on_source_lost() when a source disappears
     *   - Times out after refresh_interval_ms to re-send current state
     *   - Woken by sig_handler() on SIGINT/SIGTERM
     */

    /* Local TX queues — built under lock, transmitted outside */
    node_packet_v1_t tx_queue[CONFIG_MAX_NODES];
    int              tx_count;

    /* Monotonic reference for stats interval */
    struct timespec  stats_ref;
    clock_gettime(CLOCK_MONOTONIC, &stats_ref);
    long stats_ivl_ns = (long)stats_ivl * 1000000000L;

    while (g_running) {

        /* --- Wait for data or refresh timeout (under lock) --- */
        struct timespec abs_ts;
        make_abs_timeout(&abs_ts, cfg.refresh_interval_ms);

        pthread_mutex_lock(&gw.mutex);
        /* Spurious wake-ups are harmless: we just re-evaluate the state */
        pthread_cond_timedwait(&gw.cond, &gw.mutex, &abs_ts);

        if (!g_running) {
            pthread_mutex_unlock(&gw.mutex);
            break;
        }

        /* Snapshot dirty nodes and blackout flag, then release lock */
        int do_blackout = gw.blackout;
        gw.blackout = 0;
        tx_count = 0;

        if (!do_blackout) {
            for (int i = 0; i < cfg.num_nodes; i++) {
                node_state_t *nd = &cfg.nodes[i];
                if (nd->dirty) {
                    node_build_packet(nd, &tx_queue[tx_count++]);
                    nd->dirty = false;
                }
            }
        }

        pthread_mutex_unlock(&gw.mutex);

        /* --- Transmit (outside lock — radio TX can take several ms) --- */
#define DO_SEND(pkt_ptr) \
        do { \
            g_stats.radio_tx_total++; \
            if (dry_run) { \
                LOG_INFO("[dry-run] TX node %-2u  R=%3u G=%3u B=%3u  frame=%u", \
                         (pkt_ptr)->dst_addr, \
                         (pkt_ptr)->rgb[0], (pkt_ptr)->rgb[1], (pkt_ptr)->rgb[2], \
                         (pkt_ptr)->frame_id); \
            } else { \
                int _r = radio_send((pkt_ptr), sizeof(*(pkt_ptr))); \
                if (_r != 0) { g_stats.radio_tx_failed++; \
                               LOG_WARN("TX failed: node %u", (pkt_ptr)->dst_addr); } \
                else { LOG_DEBUG("TX node %u frame_id=%u", \
                                 (pkt_ptr)->dst_addr, (pkt_ptr)->frame_id); } \
            } \
        } while (0)

        if (do_blackout) {
            for (int i = 0; i < cfg.num_nodes; i++) {
                node_packet_v1_t pkt;
                node_build_off_packet(&cfg.nodes[i], &pkt);
                DO_SEND(&pkt);
            }
        } else {
            for (int i = 0; i < tx_count; i++)
                DO_SEND(&tx_queue[i]);
        }

        /* --- Periodic refresh: re-send current state to initialized nodes --- */
        /* This runs on every cond_timedwait timeout (≈ refresh_interval_ms).
         * If the cond was signalled early (new data), the timeout did NOT fire
         * so we skip the refresh — the node just got an update anyway.       */
        if (!do_blackout && tx_count == 0) {
            /* Woken by timeout (no new data, no blackout) → refresh */
            pthread_mutex_lock(&gw.mutex);
            int refresh_count = 0;
            node_packet_v1_t refresh_queue[CONFIG_MAX_NODES];
            for (int i = 0; i < cfg.num_nodes; i++) {
                node_state_t *nd = &cfg.nodes[i];
                if (nd->initialized && !nd->dirty)
                    node_build_packet(nd, &refresh_queue[refresh_count++]);
            }
            pthread_mutex_unlock(&gw.mutex);

            for (int i = 0; i < refresh_count; i++) {
                g_stats.radio_refresh_total++;
                DO_SEND(&refresh_queue[i]);
            }
        }

#undef DO_SEND

        /* --- Statistics --- */
        struct timespec now_ts;
        clock_gettime(CLOCK_MONOTONIC, &now_ts);
        long elapsed_ns = (long)(now_ts.tv_sec  - stats_ref.tv_sec)  * 1000000000L
                        + (long)(now_ts.tv_nsec - stats_ref.tv_nsec);
        if (elapsed_ns >= stats_ivl_ns) {
            stats_ref = now_ts;
            stats_print();
        }
    }

    /* ---- Graceful shutdown ---- */
    LOG_INFO("Shutting down...");
    if (!dry_run) {
        LOG_INFO("Sending blackout...");
        for (int i = 0; i < cfg.num_nodes; i++) {
            node_packet_v1_t pkt;
            node_build_off_packet(&cfg.nodes[i], &pkt);
            radio_send(&pkt, sizeof(pkt));
        }
    }

    sacn_recv_deinit();
    if (!dry_run) radio_close();
    pthread_cond_destroy(&gw.cond);
    pthread_mutex_destroy(&gw.mutex);

    stats_print();
    LOG_INFO("Goodbye.");
    return 0;
}
