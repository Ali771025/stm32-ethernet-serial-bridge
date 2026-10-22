#ifndef NET_DIAG_H
#define NET_DIAG_H

#include <stdint.h>

typedef enum
{
  NET_DIAG_OK = 0,
  NET_DIAG_INVALID_STATE = 1,
  NET_DIAG_NO_PCB = 2,
  NET_DIAG_BIND_FAILED = 3,
  NET_DIAG_NO_TX_BUFFER = 4
} net_diag_status_t;

typedef struct
{
  uint32_t rx_datagrams;
  uint32_t rx_bytes;
  uint32_t tx_datagrams;
  uint32_t tx_bytes;
  uint32_t tx_errors;
  uint32_t invalid_requests;
  uint32_t rate_limited_requests;
  uint32_t snapshot_responses;
  uint32_t link_up_events;
  uint32_t link_down_events;
} net_diag_counters_t;

/*
 * Creates and binds the read-only diagnostic UDP PCB on port 40001. This must be called
 * once after MX_LWIP_Init() and before the scheduler starts. The receive
 * callback then runs only in tcpip_thread context.
 */
net_diag_status_t net_diag_init_before_scheduler(void);

/* Called from the LwIP netif link callback (tcpip_thread context). */
void net_diag_link_changed(uint8_t link_is_up);

/* Debugger/diagnostic snapshot. Counters saturate at UINT32_MAX. */
void net_diag_get_counters(net_diag_counters_t *out);

#endif /* NET_DIAG_H */
