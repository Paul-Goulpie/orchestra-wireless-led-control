/*
 * orchgateway — Orchestra Wireless LED Gateway
 *
 * Receives sACN (E1.31) DMX frames from QLC+ and forwards them as
 * NRF24L01+ radio packets to up to 60 wearable LED nodes.
 *
 * Usage: orchgateway [OPTIONS]
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>

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
 * Global state
 * --------------------------------------------------------------------- */

static volatile int g_running = 1;

static void sig_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

/* -----------------------------------------------------------------------
 * Time helpers (monotonic, milliseconds)
 * --------------------------------------------------------------------- */

static int64_t mono_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* -----------------------------------------------------------------------
 * Radio transmission with per-node frame_id management
 * --------------------------------------------------------------------- */

static void tx_node(node_state_t *node, int blackout)
{
    node_packet_v1_t pkt;
    if (blackout)
        node_build_off_packet(node, &pkt);
    else
        node_build_packet(node, &pkt);

    int ret = radio_send(&pkt, sizeof(pkt));
    g_stats.radio_tx_total++;
    if (ret != 0) {
        g_stats.radio_tx_failed++;
        LOG_WARN("TX failed: node %u (%s)", node->address, node->name);
    } else {
        LOG_DEBUG("TX node %u (%s) frame_id=%u%s",
                  node->address, node->name, pkt.frame_id,
                  blackout ? " [BLACKOUT]" : "");
    }

    clock_gettime(CLOCK_MONOTONIC, &node->last_tx);
    node->dirty = false;
}

/* -----------------------------------------------------------------------
 * Help text
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
        "Configuration file format: JSON (default: " DEFAULT_CFG ")\n"
        "  If the file does not exist it is created with built-in defaults.\n"
        "\n"
        "Examples:\n"
        "  %s -v -f /tmp/myconf.json\n"
        "  %s --channel 100 --repeat-count 2\n",
        prog,
        "-h, --help",           "Show this help and exit",
        "-V, --version",        "Show version and exit",
        "-v, --verbose",        "Enable debug output",
        "-f, --config FILE",    "Configuration file",
        "-c, --channel N",      "RF channel override (0-125)",
        "-R, --data-rate RATE", "RF data rate override",
        "-r, --repeat-count N", "Extra TX repetitions per packet",
        "-t, --sacn-timeout MS","sACN silence timeout in ms",
        "-s, --stats-interval S","Statistics print interval (seconds)",
        prog, prog);
}

/* -----------------------------------------------------------------------
 * main
 * --------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    const char *config_path  = DEFAULT_CFG;
    int         verbose      = 0;
    int         stats_ivl    = 30;    /* seconds */
    int         ov_channel   = -1;
    int         ov_repeat    = -1;
    int         ov_timeout   = -1;
    const char *ov_rate      = NULL;

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
        case 'v': verbose    = 1;      break;
        case 'f': config_path = optarg; break;
        case 'c': ov_channel = atoi(optarg); break;
        case 'R': ov_rate    = optarg; break;
        case 'r': ov_repeat  = atoi(optarg); break;
        case 't': ov_timeout = atoi(optarg); break;
        case 's': stats_ivl  = atoi(optarg); break;
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

    /* Apply command-line overrides */
    if (ov_channel >= 0 && ov_channel <= 125)
        cfg.radio.channel = (uint8_t)ov_channel;
    if (ov_repeat >= 0)
        cfg.radio.repeat_count = ov_repeat;
    if (ov_timeout >= 0)
        cfg.sacn_timeout_ms = (uint32_t)ov_timeout;
    if (ov_rate) {
        if      (strcmp(ov_rate, "250kbps") == 0) cfg.radio.data_rate = RADIO_RATE_250KBPS;
        else if (strcmp(ov_rate, "2mbps")   == 0) cfg.radio.data_rate = RADIO_RATE_2MBPS;
        else                                       cfg.radio.data_rate = RADIO_RATE_1MBPS;
    }

    config_print(&cfg);

    if (cfg.num_universes == 0) { LOG_ERROR("No universes configured"); return 1; }
    if (cfg.num_nodes     == 0) { LOG_WARN("No nodes configured");             }

    /* ---- Open sACN sockets (one per unique port) ---- */
    /* Track open sockets */
    typedef struct { int fd; uint16_t port; } sock_entry_t;
    sock_entry_t socks[CONFIG_MAX_UNIVERSES];
    int num_socks = 0;
    int max_fd    = -1;

    for (int i = 0; i < cfg.num_universes; i++) {
        universe_cfg_t *u = &cfg.universes[i];

        /* Find or create socket for this port */
        int fd = -1;
        for (int j = 0; j < num_socks; j++) {
            if (socks[j].port == u->port) { fd = socks[j].fd; break; }
        }
        if (fd < 0) {
            fd = sacn_socket_create(u->port);
            if (fd < 0) {
                LOG_ERROR("Cannot open socket for port %u (universe %u)", u->port, u->id);
                continue;
            }
            socks[num_socks].fd   = fd;
            socks[num_socks].port = u->port;
            num_socks++;
            if (fd > max_fd) max_fd = fd;
        }

        /* Join multicast group */
        if (u->multicast[0])
            sacn_socket_join(fd, u->multicast);
    }

    if (num_socks == 0) {
        LOG_ERROR("No sACN sockets could be opened");
        return 1;
    }

    /* ---- Initialize radio ---- */
    if (radio_init(&cfg.radio) != 0) {
        LOG_ERROR("Radio initialization failed");
        return 1;
    }

    /* ---- Signal handlers ---- */
    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    LOG_INFO("Gateway running — %d node(s), %d universe(s). Ctrl-C to stop.",
             cfg.num_nodes, cfg.num_universes);

    /* ---- Main loop state ---- */
    int64_t last_sacn_ms    = mono_ms();
    int64_t last_refresh_ms = mono_ms();
    int64_t last_stats_ms   = mono_ms();
    int64_t stats_ivl_ms    = (int64_t)stats_ivl * 1000;
    int     sacn_timed_out  = 0;

    uint8_t rxbuf[1500];

    while (g_running) {

        /* Build fd_set from open sockets */
        fd_set rfds;
        FD_ZERO(&rfds);
        for (int i = 0; i < num_socks; i++)
            FD_SET(socks[i].fd, &rfds);

        /* Poll at most 50 ms so timers fire promptly */
        struct timeval tv = { .tv_sec = 0, .tv_usec = 50000 };
        int nready = select(max_fd + 1, &rfds, NULL, NULL, &tv);
        if (nready < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR("select: %s", strerror(errno));
            break;
        }

        int64_t now = mono_ms();

        /* ---- Receive sACN packets ---- */
        if (nready > 0) {
            for (int i = 0; i < num_socks; i++) {
                if (!FD_ISSET(socks[i].fd, &rfds)) continue;

                ssize_t len = recv(socks[i].fd, rxbuf, sizeof(rxbuf), 0);
                if (len <= 0) continue;

                sacn_packet_t sp;
                if (sacn_parse(rxbuf, (size_t)len, &sp) != 0) continue;

                g_stats.sacn_rx++;
                last_sacn_ms   = now;
                sacn_timed_out = 0;

                LOG_DEBUG("sACN univ=%u seq=%u ch=%u",
                          sp.universe, sp.sequence, sp.dmx_count);

                /* Apply to all matching nodes */
                int matched = 0;
                for (int j = 0; j < cfg.num_nodes; j++) {
                    node_state_t *nd = &cfg.nodes[j];
                    if (nd->universe_id != sp.universe) continue;
                    matched++;
                    if (node_apply_dmx(nd, sp.dmx, sp.dmx_count))
                        nd->dirty = true;
                }
                if (!matched)
                    g_stats.sacn_skipped++;
            }
        }

        /* ---- Transmit dirty nodes (differential) ---- */
        for (int i = 0; i < cfg.num_nodes; i++) {
            node_state_t *nd = &cfg.nodes[i];
            if (nd->dirty) {
                tx_node(nd, 0);
                /* Log if we could not keep up (packet was already superseded) */
                if (nd->dirty) {
                    LOG_INFO("[%lldms] Skipped queued update for node %u (%s) univ=%u",
                             (long long)now, nd->address, nd->name, nd->universe_id);
                    g_stats.sacn_skipped++;
                    nd->dirty = false;
                }
            }
        }

        /* ---- sACN timeout → blackout ---- */
        if (!sacn_timed_out &&
            (now - last_sacn_ms) > (int64_t)cfg.sacn_timeout_ms) {
            LOG_WARN("sACN timeout after %.1f s — sending blackout",
                     (double)(now - last_sacn_ms) / 1000.0);
            g_stats.net_timeout_count++;
            sacn_timed_out = 1;
            for (int i = 0; i < cfg.num_nodes; i++) {
                node_state_t *nd = &cfg.nodes[i];
                tx_node(nd, 1);                   /* all-off packet */
                memset(nd->rgb, 0, NODE_CHANNELS); /* update buffer too */
                nd->initialized = false;
            }
        }

        /* ---- Periodic refresh (re-send current state to all nodes) ---- */
        if ((now - last_refresh_ms) >= (int64_t)cfg.refresh_interval_ms) {
            last_refresh_ms = now;
            if (!sacn_timed_out) {
                for (int i = 0; i < cfg.num_nodes; i++) {
                    node_state_t *nd = &cfg.nodes[i];
                    if (!nd->dirty && nd->initialized) {
                        tx_node(nd, 0);
                        g_stats.radio_refresh_total++;
                    }
                }
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
    for (int i = 0; i < cfg.num_nodes; i++)
        tx_node(&cfg.nodes[i], 1);

    radio_close();
    for (int i = 0; i < num_socks; i++)
        close(socks[i].fd);

    stats_print();
    LOG_INFO("Goodbye.");
    return 0;
}
