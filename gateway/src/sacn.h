#ifndef SACN_H
#define SACN_H

#include <stdint.h>

/*
 * Thin wrapper around the ETC Labs sACN library (libsACN).
 *
 * The library manages its own receiver thread internally.
 * Callbacks are invoked from that thread — protect shared state with a mutex.
 */

/*
 * Called when DMX data is received on a universe.
 *
 *   universe_id   : sACN universe number
 *   dmx           : pointer to DMX slot values (dmx[0] = slot `slot_start`)
 *   slot_start    : 1-indexed number of the first slot in dmx[]
 *   slot_count    : number of slots in dmx[]
 *   ctx           : user context pointer
 */
typedef void (*sacn_data_cb_t)(uint16_t        universe_id,
                               const uint8_t  *dmx,
                               uint16_t        slot_start,
                               uint16_t        slot_count,
                               void           *ctx);

/*
 * Called when all known sources for a universe have been lost
 * (timeout or Stream_Terminated).
 */
typedef void (*sacn_lost_cb_t)(uint16_t universe_id, void *ctx);

/* Initialize the sACN library. Call once at startup. Returns 0 on success. */
int  sacn_recv_init(void);

/*
 * Create a receiver for `universe_id`.
 * `data_cb` and `lost_cb` are called from the library's internal thread.
 * Returns 0 on success, -1 on failure.
 */
int  sacn_recv_add_universe(uint16_t      universe_id,
                            sacn_data_cb_t data_cb,
                            sacn_lost_cb_t lost_cb,
                            void          *ctx);

/* Destroy all receivers and deinitialize the sACN library. */
void sacn_recv_deinit(void);

#endif /* SACN_H */
