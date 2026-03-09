#include "node.h"
#include <string.h>

bool node_apply_dmx(node_state_t  *node,
                    const uint8_t *dmx,
                    uint16_t       slot_start,
                    uint16_t       slot_count)
{
    uint8_t channels = node->num_leds * 3;

    /* Node occupies slots [dmx_start .. dmx_start + channels - 1] (1-indexed).
     * The received buffer covers  [slot_start .. slot_start + slot_count - 1].
     * Check for overlap. */
    uint16_t node_end = node->dmx_start + channels - 1;
    uint16_t slot_end = slot_start + slot_count - 1;

    if (node->dmx_start > slot_end || node_end < slot_start)
        return false; /* no overlap */

    /* Offset of the node's first channel within the dmx[] buffer */
    uint16_t buf_offset = node->dmx_start - slot_start;

    /* Number of channels we can actually copy */
    uint16_t available = slot_count - buf_offset;
    if (available > channels)
        available = channels;

    uint8_t new_rgb[NODE_CHANNELS] = {0};
    memcpy(new_rgb, dmx + buf_offset, available);

    if (memcmp(node->rgb, new_rgb, channels) == 0)
        return false; /* no change */

    memcpy(node->rgb, new_rgb, channels);
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
