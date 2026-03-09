#ifndef SACN_H
#define SACN_H

#include <stdint.h>
#include <stddef.h>

#define SACN_PORT       5568
#define SACN_MAX_DMX    512

/* Parsed sACN (E1.31) data packet */
typedef struct {
    uint16_t universe;
    uint8_t  sequence;
    uint8_t  priority;
    uint16_t dmx_count;
    uint8_t  dmx[SACN_MAX_DMX];
} sacn_packet_t;

/*
 * Parse raw UDP payload into sacn_packet_t.
 * Returns 0 on success, -1 if the packet is invalid or not a DMX data packet.
 */
int sacn_parse(const uint8_t *data, size_t len, sacn_packet_t *out);

/*
 * Create a UDP socket bound to the given port (INADDR_ANY).
 * Returns fd >= 0 on success, -1 on error.
 */
int sacn_socket_create(uint16_t port);

/*
 * Join a multicast group on an existing socket.
 * addr_str is dotted-decimal, e.g. "239.255.0.1".
 * Safe to call with a unicast address (no-op).
 * Returns 0 on success, -1 on error.
 */
int sacn_socket_join(int fd, const char *addr_str);

#endif /* SACN_H */
