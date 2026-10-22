#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include <stdint.h>

#define U8B_BOARD_ROLE_A (1U)
#define U8B_BOARD_ROLE_B (2U)

#ifndef U8B_BOARD_ROLE
#error "U8B_BOARD_ROLE must be U8B_BOARD_ROLE_A or U8B_BOARD_ROLE_B"
#endif

#if ((U8B_BOARD_ROLE != U8B_BOARD_ROLE_A) && \
     (U8B_BOARD_ROLE != U8B_BOARD_ROLE_B))
#error "Invalid U8B_BOARD_ROLE"
#endif

typedef struct
{
  uint8_t role;
  uint8_t local_ipv4[4];
  uint8_t peer_ipv4[4];
  uint8_t netmask_ipv4[4];
  uint16_t u8b_udp_port;
} board_config_t;

/* Returned storage has static lifetime and is read-only after reset. */
const board_config_t *board_config_get(void);

#endif /* BOARD_CONFIG_H */
