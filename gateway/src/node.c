#include "node.h"
#include <string.h>

bool node_apply_dmx(node_state_t *node, const uint8_t *dmx, uint16_t dmx_len)
{
    uint16_t offset   = node->dmx_start - 1;  /* convert to 0-indexed */
    uint8_t  channels = node->num_leds * 3;

    if (offset >= dmx_len)
        return false;

    uint16_t available = dmx_len - offset;
    if (available > channels)
        available = channels;

    /* Compare first, avoid unnecessary write */
    if (memcmp(node->rgb, dmx + offset, available) == 0 &&
        (available == channels || memcmp(node->rgb + available,
                                         "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
                                         "\0\0\0\0\0\0\0\0\0\0\0\0\0\0",
                                         channels - available) == 0)) {
        return false;
    }

    memset(node->rgb, 0, channels);
    memcpy(node->rgb, dmx + offset, available);
    node->initialized = true;
    return true;
}

void node_build_packet(node_state_t *node, node_packet_v1_t *pkt)
{
    node->frame_id++;
    pkt->dst_addr = node->address;
    pkt->frame_id = node->frame_id;
    memcpy(pkt->rgb, node->rgb, NODE_CHANNELS);
}

void node_build_off_packet(node_state_t *node, node_packet_v1_t *pkt)
{
    node->frame_id++;
    pkt->dst_addr = node->address;
    pkt->frame_id = node->frame_id;
    memset(pkt->rgb, 0, NODE_CHANNELS);
}
