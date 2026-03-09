/*
 * sacn.c — sACN receiver using the ETC Labs sACN library (libsACN v4).
 *
 * The library spawns its own receiver thread and calls our callbacks
 * when data arrives or sources are lost.
 *
 * https://github.com/ETCLabs/sACN
 */

#include "sacn.h"
#include "log.h"

#include <sacn/receiver.h>
#include <sacn/common.h>
#include <etcpal/error.h>

#include <string.h>

#define MAX_RECEIVERS 32

/* Per-receiver context kept alive for the duration of the session */
typedef struct {
    uint16_t        universe_id;
    sacn_data_cb_t  data_cb;
    sacn_lost_cb_t  lost_cb;
    void           *ctx;
} recv_entry_t;

static sacn_receiver_t g_handles[MAX_RECEIVERS];
static recv_entry_t    g_entries[MAX_RECEIVERS];
static int             g_count = 0;

/* -----------------------------------------------------------------------
 * Internal sACN callbacks (called from the library's receiver thread)
 * --------------------------------------------------------------------- */

static void on_universe_data(sacn_receiver_t            handle,
                             const EtcPalSockAddr       *src_addr,
                             const SacnRemoteSource     *src_info,
                             const SacnRecvUniverseData *data,
                             void                       *ctx)
{
    (void)handle;
    (void)src_addr;
    (void)src_info;

    /* Only process null-start-code DMX frames */
    if (data->start_code != kSacnStartcodeDmx)
        return;

    /* Skip packets received during the sampling period */
    if (data->is_sampling)
        return;

    recv_entry_t *e = (recv_entry_t *)ctx;
    if (e->data_cb)
        e->data_cb(data->universe_id,
                   data->values,
                   (uint16_t)data->slot_range.start_address,
                   (uint16_t)data->slot_range.address_count,
                   e->ctx);
}

static void on_sampling_period_ended(sacn_receiver_t handle, uint16_t universe, void *ctx)
{
    (void)handle; (void)universe; (void)ctx;
    /* sampling period end — no action needed */
}

static void on_sources_lost(sacn_receiver_t    handle,
                            uint16_t           universe,
                            const SacnLostSource *lost_sources,
                            size_t              num_lost_sources,
                            void               *ctx)
{
    (void)handle;

    for (size_t i = 0; i < num_lost_sources; i++) {
        LOG_WARN("sACN: source '%s' lost on universe %u (%s)",
                 lost_sources[i].name,
                 universe,
                 lost_sources[i].terminated ? "terminated" : "timeout");
    }

    recv_entry_t *e = (recv_entry_t *)ctx;
    if (e->lost_cb)
        e->lost_cb(universe, e->ctx);
}

/* -----------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------- */

int sacn_recv_init(uint32_t source_timeout_ms)
{
    etcpal_error_t err = sacn_init(NULL, NULL);
    if (err != kEtcPalErrOk) {
        LOG_ERROR("sacn_init: %s", etcpal_strerror(err));
        return -1;
    }
    sacn_receiver_set_expired_wait(source_timeout_ms);
    LOG_INFO("sACN library initialized (source timeout: %u ms)", source_timeout_ms);
    return 0;
}

int sacn_recv_add_universe(uint16_t       universe_id,
                           sacn_data_cb_t data_cb,
                           sacn_lost_cb_t lost_cb,
                           void          *ctx)
{
    if (g_count >= MAX_RECEIVERS) {
        LOG_ERROR("sacn_recv_add_universe: max receivers (%d) reached", MAX_RECEIVERS);
        return -1;
    }

    int idx = g_count;
    g_entries[idx].universe_id = universe_id;
    g_entries[idx].data_cb     = data_cb;
    g_entries[idx].lost_cb     = lost_cb;
    g_entries[idx].ctx         = ctx;

    SacnReceiverConfig cfg;
    sacn_receiver_config_init(&cfg);
    cfg.universe_id                   = universe_id;
    cfg.callbacks.universe_data        = on_universe_data;
    cfg.callbacks.sources_lost         = on_sources_lost;
    cfg.callbacks.sampling_period_ended = on_sampling_period_ended;
    cfg.callbacks.context              = &g_entries[idx];
    cfg.flags                         = kSacnReceiverOptsFilterPreviewData;
    cfg.ip_supported                  = kSacnIpV4Only;

    etcpal_error_t err = sacn_receiver_create(&cfg, &g_handles[idx], NULL);
    if (err != kEtcPalErrOk) {
        LOG_ERROR("sacn_receiver_create(universe=%u): %s",
                  universe_id, etcpal_strerror(err));
        return -1;
    }

    g_count++;
    LOG_INFO("sACN receiver created for universe %u", universe_id);
    return 0;
}

void sacn_recv_deinit(void)
{
    for (int i = 0; i < g_count; i++)
        sacn_receiver_destroy(g_handles[i]);
    g_count = 0;
    sacn_deinit();
    LOG_INFO("sACN library deinitialized");
}
