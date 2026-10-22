#include "net_diag.h"

#include <limits.h>
#include <string.h>

#include "lwip/def.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "net_bridge.h"
#include "platform_diagnostics.h"
#include "platform_reset.h"
#include "platform_supervisor.h"
#include "stm32h7xx_hal.h"
#include "uart_service.h"

#define NET_DIAG_UDP_PORT (40001U)

static struct udp_pcb *net_diag_pcb;
static struct pbuf *net_diag_tx_pbuf;
static volatile net_diag_counters_t net_diag_counters;
static uint8_t net_diag_tx_data[PLATFORM_DIAG_RESPONSE_MAX];
/* Owned exclusively by tcpip_thread through net_diag_receive(). Keeping this
 * working set static avoids consuming the bounded tcpip_thread stack. */
static platform_diag_snapshot_t net_diag_snapshot;
static net_bridge_counters_t net_diag_bridge;
static u8b_reliable_counters_t net_diag_reliable;
static u8b_backpressure_monitor_t net_diag_backpressure;
static uart_service_counters_t net_diag_uart_global;
static uart_service_channel_counters_t net_diag_uart_channel;
static uint32_t snapshot_sequence;
static uint32_t last_response_ms;
static uint8_t response_was_sent;

static void counter_increment(volatile uint32_t *value)
{
  if (*value != UINT32_MAX)
  {
    ++(*value);
  }
}

static void counter_add(volatile uint32_t *value, uint32_t amount)
{
  if (amount > (UINT32_MAX - *value))
  {
    *value = UINT32_MAX;
  }
  else
  {
    *value += amount;
  }
}

static void net_diag_receive(void *argument,
                             struct udp_pcb *pcb,
                             struct pbuf *packet,
                             const ip_addr_t *source_address,
                             u16_t source_port)
{
  uint8_t request[PLATFORM_DIAG_REQUEST_SIZE] = {0U};
  err_t send_status;
  size_t response_length = 0U;
  uint32_t now_ms;
  uint8_t channel;

  (void)argument;

  if ((pcb == NULL) || (packet == NULL) || (source_address == NULL))
  {
    if (packet != NULL)
    {
      pbuf_free(packet);
    }
    return;
  }

  counter_increment(&net_diag_counters.rx_datagrams);
  counter_add(&net_diag_counters.rx_bytes, (uint32_t)packet->tot_len);

  if ((packet->tot_len != PLATFORM_DIAG_REQUEST_SIZE) ||
      (pbuf_copy_partial(packet, request, sizeof(request), 0U) !=
       PLATFORM_DIAG_REQUEST_SIZE) ||
      (platform_diag_validate_request(request, sizeof(request)) !=
       PLATFORM_DIAG_OK))
  {
    counter_increment(&net_diag_counters.invalid_requests);
    pbuf_free(packet);
    return;
  }
  pbuf_free(packet);

  now_ms = HAL_GetTick();
  if (platform_diag_rate_allowed(now_ms,
                                 last_response_ms,
                                 response_was_sent) == 0U)
  {
    counter_increment(&net_diag_counters.rate_limited_requests);
    return;
  }
  if ((net_diag_tx_pbuf == NULL) || (net_diag_tx_pbuf->ref != 1U) ||
      (net_diag_tx_pbuf->next != NULL))
  {
    counter_increment(&net_diag_counters.tx_errors);
    return;
  }

  (void)memset(&net_diag_snapshot, 0, sizeof(net_diag_snapshot));
  platform_supervisor_get_snapshot(&net_diag_snapshot.supervisor);
  platform_reset_get_snapshot(&net_diag_snapshot.reset);
  net_bridge_get_counters(&net_diag_bridge, &net_diag_reliable);
  net_bridge_get_backpressure(&net_diag_backpressure);
  uart_service_get_counters(&net_diag_uart_global);
  net_diag_snapshot.sequence = ++snapshot_sequence;
  net_diag_snapshot.uptime_ms = now_ms;
  net_diag_snapshot.network[0] = net_diag_counters.rx_datagrams;
  net_diag_snapshot.network[1] = net_diag_counters.tx_datagrams;
  net_diag_snapshot.network[2] = net_diag_counters.tx_errors;
  net_diag_snapshot.network[3] = net_diag_bridge.tx_data_datagrams;
  net_diag_snapshot.network[4] = net_diag_bridge.rx_data_datagrams;
  net_diag_snapshot.network[5] = net_diag_bridge.tx_errors;
  net_diag_snapshot.network[6] = net_diag_bridge.rx_protocol_rejects;
  net_diag_snapshot.network[7] = net_diag_bridge.tx_window_full_deferrals;
  net_diag_snapshot.network[8] = net_diag_bridge.uart_tx_congestion_deferrals;
  net_diag_snapshot.network[9] = net_diag_reliable.retransmissions;
  net_diag_snapshot.network[10] = net_diag_reliable.delivery_failures;
  net_diag_snapshot.network[11] = net_diag_reliable.duplicates;
  net_diag_snapshot.network[12] = net_diag_reliable.reordered;
  net_diag_snapshot.network[13] = net_diag_reliable.crc_errors;
  net_diag_snapshot.network[14] = net_diag_backpressure.pressure_events;
  net_diag_snapshot.network[15] = net_diag_backpressure.maximum_duration_ms;
  net_diag_snapshot.uart_global[0] = net_diag_uart_global.process_calls;
  net_diag_snapshot.uart_global[1] = net_diag_uart_global.serviced_quanta;
  net_diag_snapshot.uart_global[2] = net_diag_uart_global.task_stack_min_words;
  net_diag_snapshot.uart_global[3] = net_diag_uart_global.unexpected_callbacks;
  for (channel = 0U; channel < PLATFORM_DIAG_CHANNEL_COUNT; ++channel)
  {
    uart_service_get_channel_counters((uint8_t)(channel + 1U),
                                      &net_diag_uart_channel);
    net_diag_snapshot.channel[channel][0] = net_diag_uart_channel.rx_bytes_dma;
    net_diag_snapshot.channel[channel][1] = net_diag_uart_channel.rx_bytes_ring;
    net_diag_snapshot.channel[channel][2] = net_diag_uart_channel.tx_bytes_queued;
    net_diag_snapshot.channel[channel][3] = net_diag_uart_channel.tx_bytes_dma;
    net_diag_snapshot.channel[channel][4] = net_diag_uart_channel.rx_reject_bytes;
    net_diag_snapshot.channel[channel][5] = net_diag_uart_channel.tx_reject_bytes;
    net_diag_snapshot.channel[channel][6] =
      ((net_diag_uart_channel.rx_ring_high_water & UINT32_C(0xFFFF)) << 16) |
      (net_diag_uart_channel.tx_ring_high_water & UINT32_C(0xFFFF));
    net_diag_snapshot.channel[channel][7] =
      ((uint32_t)net_diag_uart_channel.degraded) |
      ((uint32_t)net_diag_uart_channel.data_loss_latched << 1);
  }
  if (platform_diag_encode_snapshot(&net_diag_snapshot,
                                    net_diag_tx_data,
                                    sizeof(net_diag_tx_data),
                                    &response_length) != PLATFORM_DIAG_OK)
  {
    counter_increment(&net_diag_counters.tx_errors);
    return;
  }
  net_diag_tx_pbuf->len = (u16_t)response_length;
  net_diag_tx_pbuf->tot_len = (u16_t)response_length;
  send_status = pbuf_take(net_diag_tx_pbuf,
                          net_diag_tx_data,
                          (u16_t)response_length);
  if (send_status == ERR_OK)
  {
    send_status = udp_sendto(pcb,
                             net_diag_tx_pbuf,
                             source_address,
                             source_port);
  }
  if (send_status == ERR_OK)
  {
    counter_increment(&net_diag_counters.tx_datagrams);
    counter_increment(&net_diag_counters.snapshot_responses);
    counter_add(&net_diag_counters.tx_bytes, (uint32_t)response_length);
    last_response_ms = now_ms;
    response_was_sent = 1U;
  }
  else
  {
    counter_increment(&net_diag_counters.tx_errors);
  }
}

net_diag_status_t net_diag_init_before_scheduler(void)
{
  err_t bind_status;

  if (net_diag_pcb != NULL)
  {
    return NET_DIAG_INVALID_STATE;
  }

  net_diag_pcb = udp_new();
  if (net_diag_pcb == NULL)
  {
    return NET_DIAG_NO_PCB;
  }

  bind_status = udp_bind(net_diag_pcb, IP_ADDR_ANY, NET_DIAG_UDP_PORT);
  if (bind_status != ERR_OK)
  {
    udp_remove(net_diag_pcb);
    net_diag_pcb = NULL;
    return NET_DIAG_BIND_FAILED;
  }

  net_diag_tx_pbuf = pbuf_alloc(PBUF_TRANSPORT,
                                PLATFORM_DIAG_RESPONSE_MAX,
                                PBUF_RAM);
  if ((net_diag_tx_pbuf == NULL) || (net_diag_tx_pbuf->next != NULL))
  {
    if (net_diag_tx_pbuf != NULL)
    {
      pbuf_free(net_diag_tx_pbuf);
      net_diag_tx_pbuf = NULL;
    }
    udp_remove(net_diag_pcb);
    net_diag_pcb = NULL;
    return NET_DIAG_NO_TX_BUFFER;
  }

  udp_recv(net_diag_pcb, net_diag_receive, NULL);
  return NET_DIAG_OK;
}

void net_diag_link_changed(uint8_t link_is_up)
{
  if (link_is_up != 0U)
  {
    counter_increment(&net_diag_counters.link_up_events);
  }
  else
  {
    counter_increment(&net_diag_counters.link_down_events);
  }
}

void net_diag_get_counters(net_diag_counters_t *out)
{
  if (out != NULL)
  {
    out->rx_datagrams = net_diag_counters.rx_datagrams;
    out->rx_bytes = net_diag_counters.rx_bytes;
    out->tx_datagrams = net_diag_counters.tx_datagrams;
    out->tx_bytes = net_diag_counters.tx_bytes;
    out->tx_errors = net_diag_counters.tx_errors;
    out->invalid_requests = net_diag_counters.invalid_requests;
    out->rate_limited_requests = net_diag_counters.rate_limited_requests;
    out->snapshot_responses = net_diag_counters.snapshot_responses;
    out->link_up_events = net_diag_counters.link_up_events;
    out->link_down_events = net_diag_counters.link_down_events;
  }
}
