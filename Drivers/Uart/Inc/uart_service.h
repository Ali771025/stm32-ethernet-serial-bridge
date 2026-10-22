#ifndef UART_SERVICE_H
#define UART_SERVICE_H

#include <stddef.h>
#include <stdint.h>

#include "stm32h7xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

#define UART_SERVICE_CHANNEL_COUNT 8U
#define UART_SERVICE_RX_DMA_CAPACITY 256U
#define UART_SERVICE_RX_RING_CAPACITY 1024U
#define UART_SERVICE_TX_RING_CAPACITY 1024U
#define UART_SERVICE_ALL_CHANNEL_BITS UINT32_C(0x000000FF)

typedef enum
{
  UART_SERVICE_OK = 0,
  UART_SERVICE_INVALID_ARGUMENT,
  UART_SERVICE_HAL_ERROR,
  UART_SERVICE_NOT_STARTED
} uart_service_status_t;

typedef struct
{
  uint32_t rx_bytes_dma;
  uint32_t rx_bytes_ring;
  uint32_t tx_bytes_queued;
  uint32_t tx_bytes_dma;
  uint32_t tx_dma_uncertain_bytes;
  uint32_t rx_idle_events;
  uint32_t rx_half_events;
  uint32_t rx_full_events;
  uint32_t tx_complete_events;
  uint32_t framing_errors;
  uint32_t noise_errors;
  uint32_t overrun_errors;
  uint32_t dma_errors;
  uint32_t rx_restart_failures;
  uint32_t rx_event_overflows;
  uint32_t rx_event_high_water;
  uint32_t rx_reject_events;
  uint32_t rx_reject_bytes;
  uint32_t tx_reject_events;
  uint32_t tx_reject_bytes;
  uint32_t rx_ring_high_water;
  uint32_t tx_ring_high_water;
  uint32_t service_quanta;
  uint32_t isr_notifications;
  uint8_t degraded;
  uint8_t data_loss_latched;
} uart_service_channel_counters_t;

typedef struct
{
  uint32_t process_calls;
  uint32_t empty_process_calls;
  uint32_t serviced_quanta;
  uint32_t requeued_notifications;
  uint32_t unexpected_callbacks;
  uint32_t task_stack_min_words;
  uint32_t isr_lookup_steps_max;
  uint8_t next_channel;
} uart_service_counters_t;

/*
 * The owner task supplies exactly CH1..CH8 handles in logical-channel order.
 * The service validates the frozen peripheral binding, copies the pointers and
 * owns every ring thereafter. HAL callbacks only publish bounded metadata and
 * notify owner_task; all byte copying and HAL restart/start work is task-owned.
 */
uart_service_status_t uart_service_start(
  UART_HandleTypeDef *const handles[UART_SERVICE_CHANNEL_COUNT],
  TaskHandle_t owner_task);

/* Merge notification bits and perform at most one service quantum per channel. */
uart_service_status_t uart_service_process(uint32_t event_bits);

/*
 * Phase 07 bridge boundary. RX has one producer (UART owner task) and one
 * consumer (network core callback). TX has the inverse ownership. These APIs
 * serialize ring metadata, copy caller-owned bytes, and never call LwIP.
 */
size_t uart_service_rx_count(uint8_t logical_channel);
size_t uart_service_rx_peek(uint8_t logical_channel,
                            uint8_t *data,
                            size_t capacity);
uart_service_status_t uart_service_rx_discard(uint8_t logical_channel,
                                              size_t length);
size_t uart_service_tx_free(uint8_t logical_channel);
uart_service_status_t uart_service_tx_write(uint8_t logical_channel,
                                            const uint8_t *data,
                                            size_t length);

/* logical_channel is 1..8. Snapshots are protected against ISR updates. */
void uart_service_get_channel_counters(
  uint8_t logical_channel,
  uart_service_channel_counters_t *counters);
void uart_service_get_counters(uart_service_counters_t *counters);

#endif /* UART_SERVICE_H */
