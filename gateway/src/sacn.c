#define _GNU_SOURCE
#include "sacn.h"
#include "log.h"

#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

/* -----------------------------------------------------------------------
 * sACN / ANSI E1.31 packet offsets (UDP payload)
 *
 *  0-1   Preamble Size       (0x0010)
 *  2-3   Postamble Size      (0x0000)
 *  4-15  ACN Packet ID       "ASC-E1.17\0\0\0"
 * 16-17  Root Flags+Length
 * 18-21  Root Vector         (0x00000004  = VECTOR_ROOT_E131_DATA)
 * 22-37  CID                 (16 bytes)
 * 38-39  Framing Flags+Length
 * 40-43  Framing Vector      (0x00000002  = VECTOR_E131_DATA_PACKET)
 * 44-107 Source Name         (64 bytes, null-terminated)
 *  108   Priority
 * 109-110 Synchronization Address
 *  111   Sequence Number
 *  112   Options
 * 113-114 Universe           (big-endian)
 * 115-116 DMP Flags+Length
 *  117   DMP Vector          (0x02)
 *  118   Address Type        (0xA1)
 * 119-120 First Property Addr (0x0000)
 * 121-122 Address Increment  (0x0001)
 * 123-124 Property Count     (includes start-code byte)
 *  125   DMX Start Code      (0x00 = NULL)
 * 126..  DMX data
 * ----------------------------------------------------------------------- */

static const uint8_t ACN_ID[12] = {
    0x41, 0x53, 0x43, 0x2D, 0x45, 0x31, 0x2E, 0x31,
    0x37, 0x00, 0x00, 0x00
};

#define SACN_MIN_LEN        126u
#define OFF_PREAMBLE        0
#define OFF_ACN_ID          4
#define OFF_ROOT_VEC        18
#define OFF_FRAME_VEC       40
#define OFF_SEQUENCE        111
#define OFF_PRIORITY        108
#define OFF_UNIVERSE        113
#define OFF_DMP_VEC         117
#define OFF_DMP_ADDRTYPE    118
#define OFF_DMP_PROPCOUNT   123
#define OFF_DMX_START_CODE  125
#define OFF_DMX_DATA        126

int sacn_parse(const uint8_t *data, size_t len, sacn_packet_t *out)
{
    if (!data || !out || len < SACN_MIN_LEN)
        return -1;

    /* Preamble */
    if (((uint16_t)data[OFF_PREAMBLE] << 8 | data[OFF_PREAMBLE + 1]) != 0x0010) {
        LOG_DEBUG("sACN: bad preamble");
        return -1;
    }

    /* ACN Packet Identifier */
    if (memcmp(data + OFF_ACN_ID, ACN_ID, 12) != 0) {
        LOG_DEBUG("sACN: bad ACN ID");
        return -1;
    }

    /* Root vector: VECTOR_ROOT_E131_DATA = 4 */
    uint32_t root_vec = (uint32_t)data[OFF_ROOT_VEC]     << 24 |
                        (uint32_t)data[OFF_ROOT_VEC + 1] << 16 |
                        (uint32_t)data[OFF_ROOT_VEC + 2] <<  8 |
                        (uint32_t)data[OFF_ROOT_VEC + 3];
    if (root_vec != 0x00000004u) {
        LOG_DEBUG("sACN: bad root vector 0x%08x", root_vec);
        return -1;
    }

    /* Framing vector: VECTOR_E131_DATA_PACKET = 2 */
    uint32_t frame_vec = (uint32_t)data[OFF_FRAME_VEC]     << 24 |
                         (uint32_t)data[OFF_FRAME_VEC + 1] << 16 |
                         (uint32_t)data[OFF_FRAME_VEC + 2] <<  8 |
                         (uint32_t)data[OFF_FRAME_VEC + 3];
    if (frame_vec != 0x00000002u) {
        LOG_DEBUG("sACN: bad framing vector 0x%08x", frame_vec);
        return -1;
    }

    /* DMP vector and address type */
    if (data[OFF_DMP_VEC] != 0x02 || data[OFF_DMP_ADDRTYPE] != 0xA1) {
        LOG_DEBUG("sACN: bad DMP header");
        return -1;
    }

    /* DMX start code must be 0 (NULL start code) */
    if (data[OFF_DMX_START_CODE] != 0x00) {
        LOG_DEBUG("sACN: non-null start code 0x%02x", data[OFF_DMX_START_CODE]);
        return -1;
    }

    out->universe = (uint16_t)data[OFF_UNIVERSE] << 8 | data[OFF_UNIVERSE + 1];
    out->sequence = data[OFF_SEQUENCE];
    out->priority = data[OFF_PRIORITY];

    uint16_t prop_count = (uint16_t)data[OFF_DMP_PROPCOUNT] << 8 |
                                    data[OFF_DMP_PROPCOUNT + 1];
    /* prop_count includes the start-code byte */
    uint16_t dmx_count = (prop_count > 0) ? (uint16_t)(prop_count - 1) : 0;
    if (dmx_count > SACN_MAX_DMX)
        dmx_count = SACN_MAX_DMX;

    /* Clamp to actual received bytes */
    if ((size_t)(OFF_DMX_DATA + dmx_count) > len)
        dmx_count = (uint16_t)(len > OFF_DMX_DATA ? len - OFF_DMX_DATA : 0);

    out->dmx_count = dmx_count;
    if (dmx_count > 0)
        memcpy(out->dmx, data + OFF_DMX_DATA, dmx_count);

    return 0;
}

int sacn_socket_create(uint16_t port)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        LOG_ERROR("socket: %s", strerror(errno));
        return -1;
    }

    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("bind(port %u): %s", port, strerror(errno));
        close(fd);
        return -1;
    }

    LOG_INFO("sACN socket bound on port %u", port);
    return fd;
}

int sacn_socket_join(int fd, const char *addr_str)
{
    struct in_addr mcast;
    if (inet_pton(AF_INET, addr_str, &mcast) != 1) {
        LOG_WARN("sacn_socket_join: invalid address '%s'", addr_str);
        return -1;
    }

    /* Only join if it is a multicast address (224.0.0.0/4) */
    if ((ntohl(mcast.s_addr) >> 28) != 0xEu)
        return 0; /* unicast — nothing to do */

    struct ip_mreq mreq;
    mreq.imr_multiaddr  = mcast;
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);

    if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        LOG_WARN("IP_ADD_MEMBERSHIP %s: %s", addr_str, strerror(errno));
        return -1;
    }

    LOG_INFO("Joined multicast group %s", addr_str);
    return 0;
}
