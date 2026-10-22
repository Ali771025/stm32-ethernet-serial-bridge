#ifndef ETH_PING_PROBE_H
#define ETH_PING_PROBE_H

#include <stdint.h>

/* Debugger-only snapshot. Writers are the ETH IRQ or Ethernet/link threads;
 * readers must halt the target for a coherent multi-field observation. */
typedef struct
{
  uint32_t hal_init_status;
  uint32_t mdio_id1;
  uint32_t mdio_id2;
  uint32_t mdio_read_errors;
  uint32_t link_state;
  uint32_t eth_irqs;
  uint32_t rx_frames;
  uint32_t tx_attempts;
  uint32_t tx_errors;
  uint32_t phy_init_attempts;
  int32_t phy_init_status;
  uint32_t phy_address;
  int32_t phy_raw_link_state;
  uint32_t eth_start_attempts;
  uint32_t eth_start_errors;
} eth_ping_probe_t;

extern volatile eth_ping_probe_t u8b_eth_ping_probe;

#endif /* ETH_PING_PROBE_H */
