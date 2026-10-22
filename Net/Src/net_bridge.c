#include "net_bridge.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "board_config.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "lwip/tcpip.h"
#include "lwip/udp.h"
#include "platform_reliability_memory.h"
#include "platform_supervisor.h"
#include "u8b_backpressure.h"
#include "uart_service.h"

#define NET_BRIDGE_FULL_PAYLOAD_DATA \
  (U8B_MAX_PAYLOAD - (U8B_MAX_RECORDS * U8B_RECORD_HEADER_SIZE))

static struct udp_pcb *bridge_pcb;
static struct pbuf *bridge_tx_pbufs[ETH_TX_DESC_CNT];
static struct tcpip_callback_msg *bridge_callback_message;
static ip_addr_t peer_address;
PLATFORM_RELIABILITY_WINDOW static u8b_reliable_t bridge_path;
static volatile net_bridge_counters_t bridge_counters;
static uint8_t channel_data[U8B_RELIABLE_CHANNEL_COUNT]
                           [U8B_RELIABLE_RECORD_DATA_MAX];
static uint8_t tx_datagram[U8B_MAX_DATAGRAM];
static uint8_t rx_datagram[U8B_MAX_DATAGRAM];
static volatile uint32_t latest_poll_ms;
static uint32_t pending_since_ms;
static uint32_t last_hello_ms;
static uint32_t last_maintenance_ms;
static uint32_t last_supervisor_probe_ms;
static volatile uint8_t bridge_initialized;
static volatile uint8_t bridge_link_up;
static volatile uint8_t callback_queued;
static volatile uint8_t queue_new_requested;
static volatile uint8_t reliability_work_active;
static uint8_t pending_timer_active;
static uint8_t hello_was_sent;
static u8b_backpressure_monitor_t backpressure_monitor;
static volatile uint8_t peer_known_snapshot;
static volatile uint8_t tx_window_count_snapshot;
static volatile uint8_t network_pressure_snapshot;
static volatile uint8_t uart_tx_congested_snapshot;

static void bridge_tcpip_callback(void *context);

static void update_work_flag(void)
{
  uint8_t index;
  uint8_t pressure =
    (bridge_path.tx_count >= U8B_RELIABLE_WINDOW_SIZE) ? 1U : 0U;

  for (index = 0U; index < U8B_RELIABLE_WINDOW_SIZE; ++index)
  {
    if ((bridge_path.tx_slots[index].occupied != 0U) &&
        ((bridge_path.tx_slots[index].retry_count != 0U) ||
         (bridge_path.tx_slots[index].exhausted != 0U)))
    {
      pressure = 1U;
      break;
    }
  }
  peer_known_snapshot = bridge_path.peer_session_valid;
  tx_window_count_snapshot = bridge_path.tx_count;
  network_pressure_snapshot = pressure;
  platform_supervisor_set_network(bridge_link_up,
                                  bridge_path.peer_session_valid,
                                  bridge_path.local_session_id,
                                  bridge_path.peer_session_id);
  reliability_work_active =
    ((bridge_path.tx_count != 0U) ||
     (bridge_path.rx_count != 0U) ||
     (u8b_reliable_ack_pending(&bridge_path) != 0U)) ? 1U : 0U;
}

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

static uint8_t deadline_reached(uint32_t now, uint32_t then,
                                uint32_t interval)
{
  return ((uint32_t)(now - then) >= interval) ? 1U : 0U;
}

static err_t transmit_datagram(const uint8_t *data, size_t length)
{
  struct pbuf *packet = NULL;
  err_t status;
  uint32_t slot;

  if ((data == NULL) || (length > U8B_MAX_DATAGRAM))
  {
    return ERR_ARG;
  }
  /* low_level_output keeps a reference until Ethernet DMA completes. Never
   * alter payload or metadata while that reference is outstanding. */
  for (slot = 0U; slot < ETH_TX_DESC_CNT; ++slot)
  {
    if ((bridge_tx_pbufs[slot] != NULL) &&
        (bridge_tx_pbufs[slot]->ref == 1U))
    {
      packet = bridge_tx_pbufs[slot];
      break;
    }
  }
  if ((packet == NULL) || (packet->next != NULL))
  {
    return ERR_MEM;
  }

  packet->len = (u16_t)length;
  packet->tot_len = (u16_t)length;
  status = pbuf_take(packet, data, (u16_t)length);
  if (status == ERR_OK)
  {
    status = udp_sendto(bridge_pcb,
                        packet,
                        &peer_address,
                        board_config_get()->u8b_udp_port);
  }
  return status;
}

static void send_hello(uint32_t now_ms)
{
  size_t length;

  if ((bridge_link_up == 0U) ||
      (u8b_reliable_encode_hello(&bridge_path,
                                 tx_datagram,
                                 sizeof(tx_datagram),
                                 &length) != U8B_RELIABLE_OK))
  {
    return;
  }
  if (transmit_datagram(tx_datagram, length) == ERR_OK)
  {
    counter_increment(&bridge_counters.tx_hello_datagrams);
    last_hello_ms = now_ms;
    hello_was_sent = 1U;
  }
  else
  {
    counter_increment(&bridge_counters.tx_errors);
  }
}

static void queue_one_data_datagram(void)
{
  u8b_reliable_channel_input_t inputs[U8B_RELIABLE_CHANNEL_COUNT] = {{0}};
  u8b_reliable_queue_result_t result;
  uint8_t channel;
  u8b_reliable_status_t protocol_status;

  for (channel = 0U; channel < U8B_RELIABLE_CHANNEL_COUNT; ++channel)
  {
    inputs[channel].length = (uint16_t)uart_service_rx_peek(
      (uint8_t)(channel + 1U),
      channel_data[channel],
      sizeof(channel_data[channel]));
    inputs[channel].data = channel_data[channel];
  }

  protocol_status = u8b_reliable_queue_data(&bridge_path, inputs, &result);
  if ((protocol_status == U8B_RELIABLE_NO_DATA) ||
      (protocol_status == U8B_RELIABLE_WINDOW_FULL))
  {
    if (protocol_status == U8B_RELIABLE_WINDOW_FULL)
    {
      counter_increment(&bridge_counters.tx_window_full_deferrals);
    }
    return;
  }
  if (protocol_status != U8B_RELIABLE_OK)
  {
    counter_increment(&bridge_counters.tx_errors);
    return;
  }

  for (channel = 0U; channel < U8B_RELIABLE_CHANNEL_COUNT; ++channel)
  {
    if (result.channel_lengths[channel] != 0U)
    {
      if (uart_service_rx_discard((uint8_t)(channel + 1U),
                                  result.channel_lengths[channel]) !=
          UART_SERVICE_OK)
      {
        counter_increment(&bridge_counters.tx_errors);
        return;
      }
    }
  }
  if (result.record_count > bridge_counters.aggregate_record_high_water)
  {
    bridge_counters.aggregate_record_high_water = result.record_count;
  }
}

static void send_one_due_datagram(uint32_t now_ms)
{
  u8b_reliable_tx_view_t view;

  if ((bridge_link_up == 0U) ||
      (bridge_path.peer_session_valid == 0U) ||
      (u8b_reliable_next_tx(&bridge_path, now_ms, &view) != U8B_RELIABLE_OK))
  {
    return;
  }
  if (transmit_datagram(view.data, view.length) != ERR_OK)
  {
    counter_increment(&bridge_counters.tx_errors);
    return;
  }
  if (u8b_reliable_commit_tx(&bridge_path, view.sequence, now_ms) !=
      U8B_RELIABLE_OK)
  {
    counter_increment(&bridge_counters.tx_errors);
    return;
  }
  counter_increment(&bridge_counters.tx_data_datagrams);
  counter_add(&bridge_counters.tx_data_bytes, view.length);
}

static void send_pending_ack(void)
{
  size_t length;

  if ((bridge_link_up == 0U) ||
      (u8b_reliable_ack_pending(&bridge_path) == 0U))
  {
    return;
  }
  if ((u8b_reliable_encode_ack(&bridge_path, tx_datagram,
                               sizeof(tx_datagram), &length) !=
       U8B_RELIABLE_OK) ||
      (transmit_datagram(tx_datagram, length) != ERR_OK))
  {
    counter_increment(&bridge_counters.tx_errors);
    return;
  }
  (void)u8b_reliable_commit_ack(&bridge_path);
}

static void drain_one_delivery(void)
{
  u8b_reliable_delivery_t delivery;
  uint16_t required[U8B_RELIABLE_CHANNEL_COUNT] = {0U};
  uint8_t index;

  if (u8b_reliable_peek_delivery(&bridge_path, &delivery) != U8B_RELIABLE_OK)
  {
    uart_tx_congested_snapshot = 0U;
    return;
  }
  for (index = 0U; index < delivery.record_count; ++index)
  {
    uint8_t channel = (uint8_t)(delivery.records[index].channel_id - 1U);
    required[channel] = (uint16_t)(required[channel] +
                                   delivery.records[index].data_len);
  }
  for (index = 0U; index < U8B_RELIABLE_CHANNEL_COUNT; ++index)
  {
    if (uart_service_tx_free((uint8_t)(index + 1U)) < required[index])
    {
      counter_increment(&bridge_counters.rx_queue_full);
      counter_increment(&bridge_counters.uart_tx_congestion_deferrals);
      uart_tx_congested_snapshot = 1U;
      return;
    }
  }
  for (index = 0U; index < delivery.record_count; ++index)
  {
    if (uart_service_tx_write(delivery.records[index].channel_id,
                              delivery.records[index].data,
                              delivery.records[index].data_len) !=
        UART_SERVICE_OK)
    {
      counter_increment(&bridge_counters.rx_commit_errors);
      return;
    }
  }
  if (u8b_reliable_commit_delivery(&bridge_path, &delivery) !=
      U8B_RELIABLE_OK)
  {
    counter_increment(&bridge_counters.rx_commit_errors);
  }
  else
  {
    uart_tx_congested_snapshot = 0U;
  }
}

static void receive_data(const uint8_t *data, size_t length)
{
  u8b_reliable_status_t status =
    u8b_reliable_accept_data(&bridge_path, data, length);

  if (status == U8B_RELIABLE_OK)
  {
    counter_increment(&bridge_counters.rx_data_datagrams);
    counter_add(&bridge_counters.rx_data_bytes, (uint32_t)length);
    drain_one_delivery();
  }
  else if (status != U8B_RELIABLE_DUPLICATE)
  {
    counter_increment(&bridge_counters.rx_protocol_rejects);
  }
  send_pending_ack();
}

static void bridge_receive(void *argument,
                           struct udp_pcb *pcb,
                           struct pbuf *packet,
                           const ip_addr_t *source_address,
                           u16_t source_port)
{
  uint16_t copied;
  uint16_t packet_length;
  uint8_t peer_changed;

  (void)argument;
  (void)pcb;
  if (packet == NULL)
  {
    return;
  }
  if ((source_address == NULL) ||
      (source_port != board_config_get()->u8b_udp_port) ||
      (ip_addr_cmp(source_address, &peer_address) == 0))
  {
    counter_increment(&bridge_counters.rx_source_rejects);
    pbuf_free(packet);
    return;
  }
  if ((packet->tot_len < U8B_HEADER_SIZE) ||
      (packet->tot_len > U8B_MAX_DATAGRAM))
  {
    counter_increment(&bridge_counters.rx_protocol_rejects);
    pbuf_free(packet);
    return;
  }
  packet_length = packet->tot_len;
  copied = pbuf_copy_partial(packet, rx_datagram, packet_length, 0U);
  pbuf_free(packet);
  if (copied != packet_length)
  {
    counter_increment(&bridge_counters.rx_protocol_rejects);
    return;
  }

  if (rx_datagram[5] == (uint8_t)U8B_TYPE_HELLO)
  {
    if (u8b_reliable_accept_hello(&bridge_path,
                                  rx_datagram,
                                  copied,
                                  &peer_changed) == U8B_RELIABLE_OK)
    {
      counter_increment(&bridge_counters.rx_hello_datagrams);
      if (peer_changed != 0U)
      {
        send_hello(latest_poll_ms);
      }
    }
    else
    {
      counter_increment(&bridge_counters.rx_protocol_rejects);
    }
  }
  else if (rx_datagram[5] == (uint8_t)U8B_TYPE_DATA)
  {
    receive_data(rx_datagram, copied);
  }
  else if (rx_datagram[5] == (uint8_t)U8B_TYPE_ACK)
  {
    if (u8b_reliable_accept_ack(&bridge_path, rx_datagram, copied) !=
        U8B_RELIABLE_OK)
    {
      counter_increment(&bridge_counters.rx_protocol_rejects);
    }
  }
  else
  {
    counter_increment(&bridge_counters.rx_protocol_rejects);
  }
  update_work_flag();
}

static void bridge_tcpip_callback(void *context)
{
  uint32_t now_ms = latest_poll_ms;
  uint32_t stack_free_words = 0U;
  uint8_t queue_new;

  (void)context;
#if (INCLUDE_uxTaskGetStackHighWaterMark == 1)
  stack_free_words = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
#endif
  platform_supervisor_heartbeat(PLATFORM_SUPERVISOR_TASK_TCPIP,
                                now_ms,
                                stack_free_words);
  taskENTER_CRITICAL();
  callback_queued = 0U;
  queue_new = queue_new_requested;
  queue_new_requested = 0U;
  taskEXIT_CRITICAL();

  if ((bridge_link_up != 0U) &&
      ((hello_was_sent == 0U) ||
       (deadline_reached(now_ms,
                         last_hello_ms,
                         NET_BRIDGE_HELLO_PERIOD_MS) != 0U)))
  {
    send_hello(now_ms);
  }
  drain_one_delivery();
  send_pending_ack();
  if (queue_new != 0U)
  {
    queue_one_data_datagram();
  }
  send_one_due_datagram(now_ms);
  update_work_flag();
}

static void request_tcpip_callback(void)
{
  uint8_t post = 0U;

  taskENTER_CRITICAL();
  if (callback_queued == 0U)
  {
    callback_queued = 1U;
    post = 1U;
  }
  taskEXIT_CRITICAL();
  if (post != 0U)
  {
    if (tcpip_callbackmsg_trycallback(bridge_callback_message) == ERR_OK)
    {
      counter_increment(&bridge_counters.callback_posts);
    }
    else
    {
      taskENTER_CRITICAL();
      callback_queued = 0U;
      taskEXIT_CRITICAL();
      counter_increment(&bridge_counters.callback_post_failures);
    }
  }
}

net_bridge_status_t net_bridge_init_before_scheduler(uint32_t session_id)
{
  const board_config_t *board = board_config_get();
  err_t bind_status;
  uint32_t slot;

  if ((session_id == 0U) || (bridge_initialized != 0U))
  {
    return NET_BRIDGE_INVALID_ARGUMENT;
  }
  (void)memset((void *)&bridge_counters, 0, sizeof(bridge_counters));
  if (u8b_reliable_init(&bridge_path, session_id) != U8B_RELIABLE_OK)
  {
    return NET_BRIDGE_INVALID_ARGUMENT;
  }
  u8b_backpressure_monitor_init(&backpressure_monitor);
  IP_ADDR4(&peer_address,
           board->peer_ipv4[0], board->peer_ipv4[1],
           board->peer_ipv4[2], board->peer_ipv4[3]);

  bridge_pcb = udp_new();
  if (bridge_pcb == NULL)
  {
    return NET_BRIDGE_NO_PCB;
  }
  bind_status = udp_bind(bridge_pcb, IP_ADDR_ANY, board->u8b_udp_port);
  if (bind_status != ERR_OK)
  {
    udp_remove(bridge_pcb);
    bridge_pcb = NULL;
    return NET_BRIDGE_BIND_FAILED;
  }
  for (slot = 0U; slot < ETH_TX_DESC_CNT; ++slot)
  {
    bridge_tx_pbufs[slot] = pbuf_alloc(PBUF_TRANSPORT,
                                      U8B_MAX_DATAGRAM,
                                      PBUF_RAM);
    if ((bridge_tx_pbufs[slot] == NULL) ||
        (bridge_tx_pbufs[slot]->next != NULL))
    {
      break;
    }
  }
  if (slot != ETH_TX_DESC_CNT)
  {
    for (slot = 0U; slot < ETH_TX_DESC_CNT; ++slot)
    {
      if (bridge_tx_pbufs[slot] != NULL)
      {
        pbuf_free(bridge_tx_pbufs[slot]);
        bridge_tx_pbufs[slot] = NULL;
      }
    }
    udp_remove(bridge_pcb);
    bridge_pcb = NULL;
    return NET_BRIDGE_NO_TX_BUFFER;
  }
  bridge_callback_message = tcpip_callbackmsg_new(bridge_tcpip_callback, NULL);
  if (bridge_callback_message == NULL)
  {
    for (slot = 0U; slot < ETH_TX_DESC_CNT; ++slot)
    {
      pbuf_free(bridge_tx_pbufs[slot]);
      bridge_tx_pbufs[slot] = NULL;
    }
    udp_remove(bridge_pcb);
    bridge_pcb = NULL;
    return NET_BRIDGE_NO_CALLBACK;
  }

  udp_recv(bridge_pcb, bridge_receive, NULL);
  bridge_initialized = 1U;
  return NET_BRIDGE_OK;
}

void net_bridge_poll(uint32_t now_ms)
{
  size_t pending = 0U;
  size_t maximum_channel_pending = 0U;
  size_t channel_pending;
  uint8_t channel;
  uint8_t due = 0U;
  uint8_t normal_flush_due = 0U;
  uint8_t pressure_active;

  if (bridge_initialized == 0U)
  {
    return;
  }
  latest_poll_ms = now_ms;
  for (channel = 1U; channel <= U8B_RELIABLE_CHANNEL_COUNT; ++channel)
  {
    channel_pending = uart_service_rx_count(channel);
    pending += channel_pending;
    if (channel_pending > maximum_channel_pending)
    {
      maximum_channel_pending = channel_pending;
    }
  }
  if (pending == 0U)
  {
    pending_timer_active = 0U;
  }
  else if (pending_timer_active == 0U)
  {
    pending_since_ms = now_ms;
    pending_timer_active = 1U;
  }
  if ((pending >= NET_BRIDGE_FULL_PAYLOAD_DATA) ||
      ((pending_timer_active != 0U) &&
       (deadline_reached(now_ms,
                         pending_since_ms,
                         NET_BRIDGE_FLUSH_DEADLINE_MS) != 0U)))
  {
    normal_flush_due = 1U;
    if (u8b_backpressure_allow_queue(tx_window_count_snapshot,
                                     (uint32_t)pending,
                                     (uint32_t)maximum_channel_pending,
                                     normal_flush_due) != 0U)
    {
      due = 1U;
      pending_timer_active = 0U;
      taskENTER_CRITICAL();
      queue_new_requested = 1U;
      taskEXIT_CRITICAL();
    }
    else
    {
      counter_increment(&bridge_counters.partial_flush_deferrals);
    }
  }
  if ((bridge_link_up != 0U) &&
      ((hello_was_sent == 0U) ||
       (deadline_reached(now_ms,
                         last_hello_ms,
                         NET_BRIDGE_HELLO_PERIOD_MS) != 0U)))
  {
    due = 1U;
  }
  if ((bridge_link_up != 0U) && (reliability_work_active != 0U) &&
      (deadline_reached(now_ms, last_maintenance_ms, 5U) != 0U))
  {
    last_maintenance_ms = now_ms;
    due = 1U;
  }
  if ((uint32_t)(now_ms - last_supervisor_probe_ms) >= 250U)
  {
    last_supervisor_probe_ms = now_ms;
    due = 1U;
  }
  if (due != 0U)
  {
    request_tcpip_callback();
  }
  pressure_active = ((bridge_link_up == 0U) ||
                     (peer_known_snapshot == 0U) ||
                     (network_pressure_snapshot != 0U) ||
                     (uart_tx_congested_snapshot != 0U)) ? 1U : 0U;
  u8b_backpressure_monitor_update(&backpressure_monitor,
                                  now_ms,
                                  pressure_active);
}

void net_bridge_link_changed(uint8_t link_is_up)
{
  if (bridge_initialized == 0U)
  {
    return;
  }
  bridge_link_up = (link_is_up != 0U) ? 1U : 0U;
  if (bridge_link_up != 0U)
  {
    counter_increment(&bridge_counters.link_up_events);
    send_hello(latest_poll_ms);
  }
  else
  {
    counter_increment(&bridge_counters.link_down_events);
    hello_was_sent = 0U;
  }
  platform_supervisor_set_network(bridge_link_up,
                                  bridge_path.peer_session_valid,
                                  bridge_path.local_session_id,
                                  bridge_path.peer_session_id);
}

void net_bridge_get_counters(net_bridge_counters_t *out,
                             u8b_reliable_counters_t *protocol_out)
{
  taskENTER_CRITICAL();
  if (out != NULL)
  {
    (void)memcpy(out, (const void *)&bridge_counters, sizeof(*out));
  }
  if (protocol_out != NULL)
  {
    (void)memcpy(protocol_out,
                 &bridge_path.counters,
                 sizeof(*protocol_out));
  }
  taskEXIT_CRITICAL();
}

void net_bridge_get_backpressure(u8b_backpressure_monitor_t *out)
{
  if (out != NULL)
  {
    taskENTER_CRITICAL();
    (void)memcpy(out, &backpressure_monitor, sizeof(*out));
    taskEXIT_CRITICAL();
  }
}
