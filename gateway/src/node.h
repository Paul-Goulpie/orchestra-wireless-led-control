#ifndef NODE_H
#define NODE_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#define NODE_MAX_ADDR    63
#define NODE_MAX_LEDS    10
#define NODE_CHANNELS    30   /* 10 LEDs × 3 bytes RGB */
#define NODE_PACKET_SIZE 32

/* Must match firmware struct node_packet_v1 */
typedef struct __attribute__((packed)) {
    uint8_t dst_addr;
    uint8_t frame_id;
    uint8_t rgb[NODE_CHANNELS];
} node_packet_v1_t;

#ifdef __cplusplus
static_assert(sizeof(node_packet_v1_t) == NODE_PACKET_SIZE,
              "node_packet_v1_t size must be 32 bytes");
#else
_Static_assert(sizeof(node_packet_v1_t) == NODE_PACKET_SIZE,
               "node_packet_v1_t size must be 32 bytes");
#endif

typedef struct {
    char     name[64];
    uint8_t  address;        /* 1-63 */
    uint8_t  num_leds;       /* default 10 */
    uint16_t universe_id;    /* sACN universe carrying this node's data */
    uint16_t dmx_start;      /* 1-indexed DMX start channel in the universe */

    uint8_t  rgb[NODE_CHANNELS];  /* current RGB state (buffer) */
    uint8_t  frame_id;            /* per-node wrapping TX frame counter */
    bool     dirty;               /* has unsent update */
    bool     initialized;         /* received at least one valid sACN packet */
    struct timespec last_tx;      /* monotonic time of last radio transmission */
} node_state_t;

/*
 * Update node RGB from a DMX slot buffer.
 *
 *   dmx        : pointer to slot values, dmx[0] corresponds to slot `slot_start`
 *   slot_start : 1-indexed number of the first slot in dmx[] (from sACN slot_range)
 *   slot_count : number of slots in dmx[]
 *
 * Returns true if the node's RGB content changed.
 */
bool node_apply_dmx(node_state_t  *node,
                    const uint8_t *dmx,
                    uint16_t       slot_start,
                    uint16_t       slot_count);

/* Build a radio packet reflecting current node RGB state. */
void node_build_packet(node_state_t *node, node_packet_v1_t *pkt);

/* Build an all-off (blackout) radio packet. */
void node_build_off_packet(node_state_t *node, node_packet_v1_t *pkt);

#endif /* NODE_H */
