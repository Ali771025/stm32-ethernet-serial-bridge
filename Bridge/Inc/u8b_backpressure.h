#ifndef U8B_BACKPRESSURE_H
#define U8B_BACKPRESSURE_H

#include <stdint.h>

#define U8B_BACKPRESSURE_CHANNEL_COUNT 8U
#define U8B_BACKPRESSURE_UART_BYTES_PER_SECOND 480U
#define U8B_BACKPRESSURE_UART_RING_BYTES 1024U
#define U8B_BACKPRESSURE_WINDOW_SLOTS 32U
#define U8B_BACKPRESSURE_EFFICIENT_SLOT_BYTES 256U
#define U8B_BACKPRESSURE_FULL_PAYLOAD_DATA 1136U
#define U8B_BACKPRESSURE_CONFIGURED_OUTAGE_MS 4000U

#define U8B_BACKPRESSURE_RING_TOTAL_BYTES \
  (U8B_BACKPRESSURE_CHANNEL_COUNT * U8B_BACKPRESSURE_UART_RING_BYTES)
#define U8B_BACKPRESSURE_WINDOW_GUARANTEED_BYTES \
  (1U + ((U8B_BACKPRESSURE_WINDOW_SLOTS - 1U) * \
         U8B_BACKPRESSURE_EFFICIENT_SLOT_BYTES))
#define U8B_BACKPRESSURE_TOTAL_GUARANTEED_BYTES \
  (U8B_BACKPRESSURE_RING_TOTAL_BYTES + \
   U8B_BACKPRESSURE_WINDOW_GUARANTEED_BYTES)
#define U8B_BACKPRESSURE_AGGREGATE_BYTES_PER_SECOND \
  (U8B_BACKPRESSURE_CHANNEL_COUNT * \
   U8B_BACKPRESSURE_UART_BYTES_PER_SECOND)

typedef struct
{
  uint32_t pressure_events;
  uint32_t recovery_events;
  uint32_t current_duration_ms;
  uint32_t maximum_duration_ms;
  uint32_t pressure_started_ms;
  uint8_t active;
} u8b_backpressure_monitor_t;

/*
 * Preserve one normal low-latency partial datagram. Once any DATA is
 * outstanding, consume another UART span only when it contributes at least
 * one complete 256-byte record or the aggregate can fill a maximum-record
 * payload. This prevents an outage from filling every TX slot with tiny
 * deadline-flush datagrams.
 */
uint8_t u8b_backpressure_allow_queue(uint8_t tx_window_count,
                                     uint32_t total_pending,
                                     uint32_t maximum_channel_pending,
                                     uint8_t normal_flush_due);

uint32_t u8b_backpressure_guaranteed_outage_ms(void);
uint8_t u8b_backpressure_configuration_fits(void);

void u8b_backpressure_monitor_init(u8b_backpressure_monitor_t *monitor);
void u8b_backpressure_monitor_update(u8b_backpressure_monitor_t *monitor,
                                     uint32_t now_ms,
                                     uint8_t pressure_active);

#endif /* U8B_BACKPRESSURE_H */
