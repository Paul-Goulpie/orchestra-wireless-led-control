#ifndef CONFIG_H
#define CONFIG_H

#include "node.h"
#include "radio.h"
#include <stdint.h>

#define CONFIG_MAX_NODES     64
#define CONFIG_MAX_UNIVERSES 32

typedef struct {
    char     name[64];
    uint16_t id;
    char     multicast[40]; /* dotted-decimal or empty for unicast */
    uint16_t port;
} universe_cfg_t;

typedef struct {
    radio_config_t radio;

    /* Network */
    uint32_t sacn_timeout_ms;
    uint32_t refresh_interval_ms;

    /* Nodes */
    int          num_nodes;
    node_state_t nodes[CONFIG_MAX_NODES];

    /* Universes */
    int            num_universes;
    universe_cfg_t universes[CONFIG_MAX_UNIVERSES];
} app_config_t;

/*
 * Load configuration from path.
 * If the file does not exist, write the built-in default and use it.
 * Returns 0 on success, -1 on failure.
 */
int config_load(const char *path, app_config_t *cfg);

/* Print a human-readable summary of the loaded configuration. */
void config_print(const app_config_t *cfg);

/* Find a universe by its sACN universe id. Returns NULL if not found. */
universe_cfg_t *config_find_universe(app_config_t *cfg, uint16_t universe_id);

#endif /* CONFIG_H */
