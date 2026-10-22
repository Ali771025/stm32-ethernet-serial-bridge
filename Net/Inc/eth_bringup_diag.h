#ifndef ETH_BRINGUP_DIAG_H
#define ETH_BRINGUP_DIAG_H

#include <stdint.h>

/* Snapshot is written only by defaultTask. Halt the target before reading
 * multiple fields in a debugger; individual 32-bit fields are diagnostic. */
typedef struct
{
  uint32_t sample_ms;
  uint32_t samples;
  uint32_t lwip_netif_ready;
  uint32_t phy_init_attempts;
  int32_t phy_init_status;
  uint32_t phy_id1;
  uint32_t phy_id2;
  int32_t phy_link_state;
  uint32_t eth_start_attempts;
  uint32_t eth_start_errors;
  uint32_t eth_irqs;
  uint32_t rx_frames;
  uint32_t tx_attempts;
  uint32_t tx_errors;
  uint32_t stage;
} eth_bringup_diag_t;

typedef enum
{
  ETH_BRINGUP_BOOT = 0,
  ETH_BRINGUP_WAIT_PHY_INIT = 1,
  ETH_BRINGUP_PHY_INIT_FAILED = 2,
  ETH_BRINGUP_WAIT_LINK = 3,
  ETH_BRINGUP_WAIT_MAC_START = 4,
  ETH_BRINGUP_MAC_START_FAILED = 5,
  ETH_BRINGUP_WAIT_TRAFFIC = 6,
  ETH_BRINGUP_RX_WITHOUT_TX = 7,
  ETH_BRINGUP_TX_WITHOUT_RX = 8,
  ETH_BRINGUP_RX_AND_TX = 9
} eth_bringup_stage_t;

/* Pure, host-testable classifier. Stages are observations, not a claim that
 * ICMP succeeded. LAN8742 link modes 2..5 are the only usable modes. */
static inline eth_bringup_stage_t eth_bringup_classify(
  const eth_bringup_diag_t *sample)
{
  if ((sample == (const eth_bringup_diag_t *)0) ||
      (sample->lwip_netif_ready == 0U))
  {
    return ETH_BRINGUP_BOOT;
  }
  if (sample->phy_init_attempts == 0U)
  {
    return ETH_BRINGUP_WAIT_PHY_INIT;
  }
  if (sample->phy_init_status != 0)
  {
    return ETH_BRINGUP_PHY_INIT_FAILED;
  }
  if ((sample->phy_link_state < 2) || (sample->phy_link_state > 5))
  {
    return ETH_BRINGUP_WAIT_LINK;
  }
  if (sample->eth_start_attempts == 0U)
  {
    return ETH_BRINGUP_WAIT_MAC_START;
  }
  if (sample->eth_start_attempts <= sample->eth_start_errors)
  {
    return ETH_BRINGUP_MAC_START_FAILED;
  }
  if ((sample->rx_frames != 0U) && (sample->tx_attempts != 0U))
  {
    return ETH_BRINGUP_RX_AND_TX;
  }
  if (sample->rx_frames != 0U)
  {
    return ETH_BRINGUP_RX_WITHOUT_TX;
  }
  if (sample->tx_attempts != 0U)
  {
    return ETH_BRINGUP_TX_WITHOUT_RX;
  }
  return ETH_BRINGUP_WAIT_TRAFFIC;
}

extern volatile eth_bringup_diag_t u8b_eth_bringup_diag;
void eth_bringup_diag_poll(uint32_t now_ms);

#endif /* ETH_BRINGUP_DIAG_H */
