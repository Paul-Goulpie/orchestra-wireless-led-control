/*
 * config.c — JSON configuration loader.
 *
 * The built-in default configuration is embedded via the xxd-generated header
 * default_config_data.h (created by `make` from resources/default_config.json).
 */

#include "config.h"
#include "sacn.h"
#include "log.h"
#include "default_config_data.h"

#include <cjson/cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* -----------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------- */

static radio_rate_t parse_rate(const char *s)
{
    if (!s)                        return RADIO_RATE_1MBPS;
    if (strcmp(s, "250kbps") == 0) return RADIO_RATE_250KBPS;
    if (strcmp(s, "2mbps")   == 0) return RADIO_RATE_2MBPS;
    return RADIO_RATE_1MBPS;
}

static void set_defaults(app_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->radio.spi_device, "/dev/spidev0.0",
            sizeof(cfg->radio.spi_device) - 1);
    cfg->radio.spi_speed_hz     = 10000000;
    cfg->radio.ce_pin           = 25;
    cfg->radio.channel          = 76;
    cfg->radio.data_rate        = RADIO_RATE_1MBPS;
    cfg->radio.repeat_count     = 1;
    cfg->sacn_timeout_ms        = 2000;
    cfg->refresh_interval_ms    = 1000;
}

/* -----------------------------------------------------------------------
 * JSON parsing
 * --------------------------------------------------------------------- */

static int parse_json(const char *json_str, app_config_t *cfg)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        const char *err = cJSON_GetErrorPtr();
        LOG_ERROR("JSON parse error near: %s", err ? err : "unknown");
        return -1;
    }

    set_defaults(cfg);

    /* --- radio --- */
    cJSON *radio = cJSON_GetObjectItemCaseSensitive(root, "radio");
    if (cJSON_IsObject(radio)) {
        cJSON *item;
#define GET_STR(key, dst) \
        if ((item = cJSON_GetObjectItemCaseSensitive(radio, key)) && cJSON_IsString(item)) \
            strncpy(dst, item->valuestring, sizeof(dst) - 1);
#define GET_U32(key, dst) \
        if ((item = cJSON_GetObjectItemCaseSensitive(radio, key)) && cJSON_IsNumber(item)) \
            (dst) = (uint32_t)(item->valuedouble);
#define GET_U8(key, dst) \
        if ((item = cJSON_GetObjectItemCaseSensitive(radio, key)) && cJSON_IsNumber(item)) \
            (dst) = (uint8_t)(item->valuedouble);
#define GET_INT(key, dst) \
        if ((item = cJSON_GetObjectItemCaseSensitive(radio, key)) && cJSON_IsNumber(item)) \
            (dst) = (int)(item->valuedouble);

        GET_STR("spi_device",    cfg->radio.spi_device)
        GET_U32("spi_speed_hz",  cfg->radio.spi_speed_hz)
        GET_U8 ("ce_pin",        cfg->radio.ce_pin)
        GET_U8 ("channel",       cfg->radio.channel)
        GET_INT("repeat_count",  cfg->radio.repeat_count)

        if ((item = cJSON_GetObjectItemCaseSensitive(radio, "data_rate")) &&
            cJSON_IsString(item))
            cfg->radio.data_rate = parse_rate(item->valuestring);

#undef GET_STR
#undef GET_U32
#undef GET_U8
#undef GET_INT
    }

    /* --- network --- */
    cJSON *net = cJSON_GetObjectItemCaseSensitive(root, "network");
    if (cJSON_IsObject(net)) {
        cJSON *item;
        if ((item = cJSON_GetObjectItemCaseSensitive(net, "sacn_timeout_ms")) &&
            cJSON_IsNumber(item))
            cfg->sacn_timeout_ms = (uint32_t)item->valuedouble;
        if ((item = cJSON_GetObjectItemCaseSensitive(net, "refresh_interval_ms")) &&
            cJSON_IsNumber(item))
            cfg->refresh_interval_ms = (uint32_t)item->valuedouble;
    }

    /* --- universes --- */
    cJSON *univs = cJSON_GetObjectItemCaseSensitive(root, "universes");
    if (cJSON_IsArray(univs)) {
        int n = cJSON_GetArraySize(univs);
        for (int i = 0; i < n && cfg->num_universes < CONFIG_MAX_UNIVERSES; i++) {
            cJSON *u = cJSON_GetArrayItem(univs, i);
            if (!cJSON_IsObject(u)) continue;

            universe_cfg_t *uc = &cfg->universes[cfg->num_universes];
            uc->port = SACN_PORT;

            cJSON *item;
            if ((item = cJSON_GetObjectItemCaseSensitive(u, "name")) &&
                cJSON_IsString(item))
                strncpy(uc->name, item->valuestring, sizeof(uc->name) - 1);
            if ((item = cJSON_GetObjectItemCaseSensitive(u, "id")) &&
                cJSON_IsNumber(item))
                uc->id = (uint16_t)item->valuedouble;
            if ((item = cJSON_GetObjectItemCaseSensitive(u, "multicast")) &&
                cJSON_IsString(item))
                strncpy(uc->multicast, item->valuestring, sizeof(uc->multicast) - 1);
            if ((item = cJSON_GetObjectItemCaseSensitive(u, "port")) &&
                cJSON_IsNumber(item))
                uc->port = (uint16_t)item->valuedouble;

            cfg->num_universes++;
        }
    }

    /* --- nodes --- */
    cJSON *nodes = cJSON_GetObjectItemCaseSensitive(root, "nodes");
    if (cJSON_IsArray(nodes)) {
        int n = cJSON_GetArraySize(nodes);
        for (int i = 0; i < n && cfg->num_nodes < CONFIG_MAX_NODES; i++) {
            cJSON *nd = cJSON_GetArrayItem(nodes, i);
            if (!cJSON_IsObject(nd)) continue;

            node_state_t *ns = &cfg->nodes[cfg->num_nodes];
            memset(ns, 0, sizeof(*ns));
            ns->num_leds = 10;

            cJSON *item;
            if ((item = cJSON_GetObjectItemCaseSensitive(nd, "name")) &&
                cJSON_IsString(item))
                strncpy(ns->name, item->valuestring, sizeof(ns->name) - 1);
            if ((item = cJSON_GetObjectItemCaseSensitive(nd, "address")) &&
                cJSON_IsNumber(item))
                ns->address = (uint8_t)item->valuedouble;
            if ((item = cJSON_GetObjectItemCaseSensitive(nd, "num_leds")) &&
                cJSON_IsNumber(item))
                ns->num_leds = (uint8_t)item->valuedouble;
            if ((item = cJSON_GetObjectItemCaseSensitive(nd, "universe_id")) &&
                cJSON_IsNumber(item))
                ns->universe_id = (uint16_t)item->valuedouble;
            if ((item = cJSON_GetObjectItemCaseSensitive(nd, "dmx_start")) &&
                cJSON_IsNumber(item))
                ns->dmx_start = (uint16_t)item->valuedouble;

            if (ns->address < 1 || ns->address > NODE_MAX_ADDR) {
                LOG_WARN("Config: node '%s' has invalid address %u — skipped",
                         ns->name, ns->address);
                continue;
            }
            if (ns->dmx_start < 1) {
                LOG_WARN("Config: node '%s' dmx_start must be >= 1 — defaulting to 1",
                         ns->name);
                ns->dmx_start = 1;
            }
            cfg->num_nodes++;
        }
    }

    cJSON_Delete(root);
    return 0;
}

/* -----------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------- */

int config_load(const char *path, app_config_t *cfg)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        LOG_WARN("Config '%s' not found — writing built-in default", path);

        /* Try to create the default config file */
        FILE *wf = fopen(path, "w");
        if (wf) {
            fwrite(default_config_data, 1, default_config_data_len, wf);
            fclose(wf);
            LOG_INFO("Default config written to '%s'", path);
        } else {
            LOG_WARN("Cannot write default config to '%s': %s", path, strerror(errno));
        }

        /* Parse from embedded data regardless */
        char *buf = (char *)malloc(default_config_data_len + 1);
        if (!buf) return -1;
        memcpy(buf, default_config_data, default_config_data_len);
        buf[default_config_data_len] = '\0';
        int ret = parse_json(buf, cfg);
        free(buf);
        return ret;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return -1; }

    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        LOG_ERROR("Error reading '%s'", path);
        free(buf);
        fclose(f);
        return -1;
    }
    buf[sz] = '\0';
    fclose(f);

    int ret = parse_json(buf, cfg);
    free(buf);
    return ret;
}

void config_print(const app_config_t *cfg)
{
    const char *rate_str =
        (cfg->radio.data_rate == RADIO_RATE_250KBPS) ? "250kbps" :
        (cfg->radio.data_rate == RADIO_RATE_2MBPS)   ? "2mbps"   : "1mbps";

    LOG_INFO("--- Configuration ---");
    LOG_INFO("  Radio: spi=%s speed=%u ce=%u ch=%u rate=%s repeat=%d",
             cfg->radio.spi_device, cfg->radio.spi_speed_hz,
             cfg->radio.ce_pin, cfg->radio.channel,
             rate_str, cfg->radio.repeat_count);
    LOG_INFO("  Network: sacn_timeout=%ums refresh=%ums",
             cfg->sacn_timeout_ms, cfg->refresh_interval_ms);
    LOG_INFO("  Universes (%d):", cfg->num_universes);
    for (int i = 0; i < cfg->num_universes; i++) {
        const universe_cfg_t *u = &cfg->universes[i];
        LOG_INFO("    [%d] id=%-5u  mc=%-16s  port=%u  name=%s",
                 i, u->id, u->multicast[0] ? u->multicast : "-",
                 u->port, u->name);
    }
    LOG_INFO("  Nodes (%d):", cfg->num_nodes);
    for (int i = 0; i < cfg->num_nodes; i++) {
        const node_state_t *n = &cfg->nodes[i];
        LOG_INFO("    [%d] addr=%-2u  leds=%u  univ=%-5u  dmx_start=%-4u  name=%s",
                 i, n->address, n->num_leds,
                 n->universe_id, n->dmx_start, n->name);
    }
    LOG_INFO("---------------------");
}

universe_cfg_t *config_find_universe(app_config_t *cfg, uint16_t universe_id)
{
    for (int i = 0; i < cfg->num_universes; i++)
        if (cfg->universes[i].id == universe_id)
            return &cfg->universes[i];
    return NULL;
}
