#ifndef NET_BRIDGE_H
#define NET_BRIDGE_H

#include <stdint.h>

#include "u8b_backpressure.h"
#include "u8b_reliable.h"

#define NET_BRIDGE_FLUSH_DEADLINE_MS 5U
#define NET_BRIDGE_HELLO_PERIOD_MS 1000U

typedef enum
{
  NET_BRIDGE_OK = 0,
  NET_BRIDGE_INVALID_ARGUMENT,
  NET_BRIDGE_INVALID_STATE,
  NET_BRIDGE_NO_PCB,
  NET_BRIDGE_BIND_FAILED,
  NET_BRIDGE_NO_CALLBACK,
  NET_BRIDGE_NO_TX_BUFFER
} net_bridge_status_t;

typedef struct
{
  uint32_t callback_posts;
  uint32_t callback_post_failures;
  uint32_t tx_data_datagrams;
  uint32_t tx_data_bytes;
  uint32_t tx_hello_datagrams;
  uint32_t tx_errors;
  uint32_t rx_data_datagrams;
  uint32_t rx_data_bytes;
  uint32_t rx_hello_datagrams;
  uint32_t rx_source_rejects;
  uint32_t rx_protocol_rejects;
  uint32_t rx_queue_full;
  uint32_t rx_commit_errors;
  uint32_t partial_flush_deferrals;
  uint32_t tx_window_full_deferrals;
  uint32_t uart_tx_congestion_deferrals;
  uint32_t link_up_events;
  uint32_t link_down_events;
  uint32_t aggregate_record_high_water;
} net_bridge_counters_t;

/* Allocate all persistent LwIP objects before osKernelStart(). */
net_bridge_status_t net_bridge_init_before_scheduler(uint32_t session_id);

/* Called by the UART owner task. It only posts a preallocated callback. */
void net_bridge_poll(uint32_t now_ms);

/* Called from the LwIP netif link callback (tcpip_thread context). */
void net_bridge_link_changed(uint8_t link_is_up);

void net_bridge_get_counters(net_bridge_counters_t *out,
                             u8b_reliable_counters_t *protocol_out);
void net_bridge_get_backpressure(u8b_backpressure_monitor_t *out);

#endif /* NET_BRIDGE_H */
