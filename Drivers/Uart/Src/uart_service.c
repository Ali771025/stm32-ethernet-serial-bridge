#include "uart_service.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "byte_ring.h"
#include "platform_dma_memory.h"
#include "uart_round_robin.h"

#define UART_SERVICE_EVENT_CAPACITY 8U
#define UART_SERVICE_CHUNK 256U

typedef struct
{
  uint16_t position;
} uart_rx_event_t;

typedef struct
{
  UART_HandleTypeDef *uart;
  byte_ring_t rx_ring;
  byte_ring_t tx_ring;
  volatile uart_rx_event_t events[UART_SERVICE_EVENT_CAPACITY];
  volatile uint8_t event_head;
  volatile uint8_t event_tail;
  volatile uint8_t tx_active;
  volatile uint8_t tx_complete_pending;
  volatile uint8_t rx_restart_pending;
  volatile uint16_t tx_dma_length;
  uint16_t rx_position;
  volatile uint8_t started;
  volatile uart_service_channel_counters_t counters;
} uart_channel_t;

static USART_TypeDef *const expected_instances[UART_SERVICE_CHANNEL_COUNT] =
{
  USART1, USART2, USART3, UART4, UART5, USART6, UART7, UART8
};
static DMA_Stream_TypeDef *const expected_rx_dma[UART_SERVICE_CHANNEL_COUNT] =
{
  DMA1_Stream0, DMA1_Stream2, DMA2_Stream0, DMA2_Stream2,
  DMA1_Stream4, DMA1_Stream5, DMA1_Stream6, DMA1_Stream7
};
static DMA_Stream_TypeDef *const expected_tx_dma[UART_SERVICE_CHANNEL_COUNT] =
{
  DMA1_Stream1, DMA1_Stream3, DMA2_Stream1, DMA2_Stream3,
  DMA2_Stream4, DMA2_Stream5, DMA2_Stream6, DMA2_Stream7
};

static uart_channel_t channels[UART_SERVICE_CHANNEL_COUNT];
static uint8_t rx_storage[UART_SERVICE_CHANNEL_COUNT]
                         [UART_SERVICE_RX_RING_CAPACITY];
static uint8_t tx_storage[UART_SERVICE_CHANNEL_COUNT]
                         [UART_SERVICE_TX_RING_CAPACITY];
static uint8_t service_buffer[UART_SERVICE_CHUNK];
PLATFORM_DMA_BUFFER static uint8_t rx_dma[UART_SERVICE_CHANNEL_COUNT]
                                           [UART_SERVICE_RX_DMA_CAPACITY];
PLATFORM_DMA_BUFFER static uint8_t tx_dma[UART_SERVICE_CHANNEL_COUNT]
                                           [UART_SERVICE_CHUNK];
static TaskHandle_t owner_task;
static uint8_t service_started;
static uint8_t round_robin_cursor;
static uint32_t task_pending_bits;
static volatile uart_service_counters_t service_counters;

static uint32_t add_u32(uint32_t value, uint32_t increment)
{
  return (increment > (UINT32_MAX - value)) ? UINT32_MAX : value + increment;
}

static void mark_degraded(uart_channel_t *channel)
{
  channel->counters.degraded = 1U;
}

static void mark_data_loss(uart_channel_t *channel)
{
  channel->counters.data_loss_latched = 1U;
  mark_degraded(channel);
}

static uart_channel_t *find_channel(UART_HandleTypeDef *uart,
                                    uint8_t *channel_index)
{
  uint8_t index;
  uint32_t steps;

  for (index = 0U; index < UART_SERVICE_CHANNEL_COUNT; ++index)
  {
    steps = (uint32_t)index + 1U;
    if (channels[index].uart == uart)
    {
      if (steps > service_counters.isr_lookup_steps_max)
      {
        service_counters.isr_lookup_steps_max = steps;
      }
      if (channel_index != NULL)
      {
        *channel_index = index;
      }
      return &channels[index];
    }
  }
  service_counters.isr_lookup_steps_max = UART_SERVICE_CHANNEL_COUNT;
  return NULL;
}

static void notify_owner_from_isr(uart_channel_t *channel, uint8_t index)
{
  BaseType_t higher_priority_task_woken = pdFALSE;

  channel->counters.isr_notifications =
    add_u32(channel->counters.isr_notifications, 1U);
  if (owner_task != NULL)
  {
    (void)xTaskNotifyFromISR(owner_task,
                            UINT32_C(1) << index,
                            eSetBits,
                            &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
  }
}

static void publish_rx_event_from_isr(uart_channel_t *channel,
                                      uint8_t index,
                                      uint16_t position)
{
  uint8_t head;
  uint8_t next;
  uint8_t tail;
  uint8_t depth;

  if (position >= UART_SERVICE_RX_DMA_CAPACITY)
  {
    position = 0U;
  }
  head = channel->event_head;
  next = (uint8_t)((head + 1U) % UART_SERVICE_EVENT_CAPACITY);
  tail = channel->event_tail;
  if (next == tail)
  {
    channel->counters.rx_event_overflows =
      add_u32(channel->counters.rx_event_overflows, 1U);
    mark_data_loss(channel);
    notify_owner_from_isr(channel, index);
    return;
  }

  channel->events[head].position = position;
  __DMB();
  channel->event_head = next;
  depth = (uint8_t)((next + UART_SERVICE_EVENT_CAPACITY - tail) %
                    UART_SERVICE_EVENT_CAPACITY);
  if (depth > channel->counters.rx_event_high_water)
  {
    channel->counters.rx_event_high_water = depth;
  }
  notify_owner_from_isr(channel, index);
}

static uint8_t take_rx_event(uart_channel_t *channel, uart_rx_event_t *event)
{
  uint8_t tail;

  tail = channel->event_tail;
  if (tail == channel->event_head)
  {
    return 0U;
  }
  __DMB();
  event->position = channel->events[tail].position;
  channel->event_tail =
    (uint8_t)((tail + 1U) % UART_SERVICE_EVENT_CAPACITY);
  return 1U;
}

static void account_ring_counters(uart_channel_t *channel)
{
  channel->counters.rx_reject_events = channel->rx_ring.reject_events;
  channel->counters.rx_reject_bytes = channel->rx_ring.reject_bytes;
  channel->counters.tx_reject_events = channel->tx_ring.reject_events;
  channel->counters.tx_reject_bytes = channel->tx_ring.reject_bytes;
  channel->counters.rx_ring_high_water =
    (uint32_t)channel->rx_ring.high_water;
  channel->counters.tx_ring_high_water =
    (uint32_t)channel->tx_ring.high_water;
  if ((channel->rx_ring.reject_events != 0U) ||
      (channel->tx_ring.reject_events != 0U))
  {
    mark_degraded(channel);
  }
}

static void accept_dma_event(uart_channel_t *channel,
                             uint8_t index,
                             uint16_t position)
{
  byte_ring_status_t status;
  uint16_t length;
  uint16_t first;

  if (position >= channel->rx_position)
  {
    length = (uint16_t)(position - channel->rx_position);
  }
  else
  {
    length = (uint16_t)((UART_SERVICE_RX_DMA_CAPACITY -
                         channel->rx_position) + position);
  }
  if (length == 0U)
  {
    return;
  }

  first = (uint16_t)(UART_SERVICE_RX_DMA_CAPACITY - channel->rx_position);
  if (first > length)
  {
    first = length;
  }
  (void)memcpy(service_buffer,
               &rx_dma[index][channel->rx_position],
               first);
  if (length > first)
  {
    (void)memcpy(&service_buffer[first],
                 rx_dma[index],
                 (size_t)(length - first));
  }
  taskENTER_CRITICAL();
  status = byte_ring_write(&channel->rx_ring, service_buffer, length);
  taskEXIT_CRITICAL();
  channel->counters.rx_bytes_dma =
    add_u32(channel->counters.rx_bytes_dma, length);
  if (status == BYTE_RING_OK)
  {
    channel->counters.rx_bytes_ring =
      add_u32(channel->counters.rx_bytes_ring, length);
  }
  else
  {
    mark_data_loss(channel);
  }
}

static uint8_t consume_one_rx_event(uart_channel_t *channel, uint8_t index)
{
  uart_rx_event_t event;

  if (take_rx_event(channel, &event) == 0U)
  {
    return 0U;
  }
  accept_dma_event(channel, index, event.position);
  channel->rx_position = event.position;
  return 1U;
}

static void consume_all_rx_events(uart_channel_t *channel, uint8_t index)
{
  while (consume_one_rx_event(channel, index) != 0U)
  {
    /* Bounded by the seven usable entries in the fixed metadata queue. */
  }
}

static uart_service_status_t start_tx_if_idle(uart_channel_t *channel,
                                               uint8_t index)
{
  size_t length;
  HAL_StatusTypeDef status;

  if (channel->tx_active != 0U)
  {
    return UART_SERVICE_OK;
  }
  taskENTER_CRITICAL();
  length = byte_ring_peek(&channel->tx_ring,
                          tx_dma[index],
                          sizeof(tx_dma[index]));
  taskEXIT_CRITICAL();
  if (length == 0U)
  {
    return UART_SERVICE_OK;
  }

  channel->tx_dma_length = (uint16_t)length;
  channel->tx_active = 1U;
  __DMB();
  status = HAL_UART_Transmit_DMA(channel->uart,
                                 tx_dma[index],
                                 (uint16_t)length);
  if (status != HAL_OK)
  {
    channel->tx_active = 0U;
    channel->tx_dma_length = 0U;
    taskENTER_CRITICAL();
    channel->counters.dma_errors =
      add_u32(channel->counters.dma_errors, 1U);
    taskEXIT_CRITICAL();
    mark_degraded(channel);
    return UART_SERVICE_HAL_ERROR;
  }
  taskENTER_CRITICAL();
  (void)byte_ring_discard(&channel->tx_ring, length);
  taskEXIT_CRITICAL();
  return UART_SERVICE_OK;
}

static void restart_rx_if_required(uart_channel_t *channel, uint8_t index)
{
  HAL_StatusTypeDef status;

  if (channel->rx_restart_pending == 0U)
  {
    return;
  }

  /* Preserve every already-published byte before resetting DMA metadata. */
  consume_all_rx_events(channel, index);
  channel->rx_restart_pending = 0U;
  (void)HAL_UART_AbortReceive(channel->uart);
  channel->rx_position = 0U;
  taskENTER_CRITICAL();
  channel->event_head = 0U;
  channel->event_tail = 0U;
  taskEXIT_CRITICAL();
  status = HAL_UARTEx_ReceiveToIdle_DMA(channel->uart,
                                       rx_dma[index],
                                       UART_SERVICE_RX_DMA_CAPACITY);
  if (status != HAL_OK)
  {
    channel->rx_restart_pending = 1U;
    taskENTER_CRITICAL();
    channel->counters.rx_restart_failures =
      add_u32(channel->counters.rx_restart_failures, 1U);
    taskEXIT_CRITICAL();
    mark_degraded(channel);
  }
}

static uint8_t channel_has_task_work(const uart_channel_t *channel)
{
  size_t tx_count;

  if ((channel->event_head != channel->event_tail) ||
      (channel->rx_restart_pending != 0U))
  {
    return 1U;
  }
  taskENTER_CRITICAL();
  tx_count = byte_ring_count(&channel->tx_ring);
  taskEXIT_CRITICAL();
  if ((channel->tx_active == 0U) && (tx_count != 0U))
  {
    return 1U;
  }
  return 0U;
}

static uart_service_status_t service_one(uint8_t index)
{
  uart_channel_t *channel = &channels[index];
  uart_service_status_t status;

  if (channel->tx_complete_pending != 0U)
  {
    channel->tx_complete_pending = 0U;
  }
  (void)consume_one_rx_event(channel, index);
  restart_rx_if_required(channel, index);
  status = start_tx_if_idle(channel, index);
  taskENTER_CRITICAL();
  account_ring_counters(channel);
  taskEXIT_CRITICAL();
  channel->counters.service_quanta =
    add_u32(channel->counters.service_quanta, 1U);
  return status;
}

uart_service_status_t uart_service_start(
  UART_HandleTypeDef *const handles[UART_SERVICE_CHANNEL_COUNT],
  TaskHandle_t task)
{
  uint8_t index;
  HAL_StatusTypeDef hal_status;

  if ((handles == NULL) || (task == NULL))
  {
    return UART_SERVICE_INVALID_ARGUMENT;
  }
  for (index = 0U; index < UART_SERVICE_CHANNEL_COUNT; ++index)
  {
    if ((handles[index] == NULL) ||
        (handles[index]->Instance != expected_instances[index]) ||
        (handles[index]->hdmarx == NULL) ||
        (handles[index]->hdmatx == NULL) ||
        (handles[index]->hdmarx->Instance != expected_rx_dma[index]) ||
        (handles[index]->hdmatx->Instance != expected_tx_dma[index]) ||
        (handles[index]->hdmarx->Init.Mode != DMA_CIRCULAR) ||
        (handles[index]->hdmatx->Init.Mode != DMA_NORMAL) ||
        (handles[index]->hdmarx->Init.Direction != DMA_PERIPH_TO_MEMORY) ||
        (handles[index]->hdmatx->Init.Direction != DMA_MEMORY_TO_PERIPH) ||
        (handles[index]->hdmarx->Init.MemInc != DMA_MINC_ENABLE) ||
        (handles[index]->hdmatx->Init.MemInc != DMA_MINC_ENABLE) ||
        (handles[index]->hdmarx->Init.MemDataAlignment != DMA_MDATAALIGN_BYTE) ||
        (handles[index]->hdmatx->Init.MemDataAlignment != DMA_MDATAALIGN_BYTE))
    {
      return UART_SERVICE_INVALID_ARGUMENT;
    }
  }

  (void)memset(channels, 0, sizeof(channels));
  (void)memset((void *)&service_counters, 0, sizeof(service_counters));
  owner_task = task;
  round_robin_cursor = 0U;
  task_pending_bits = UART_SERVICE_ALL_CHANNEL_BITS;
  service_started = 0U;

  for (index = 0U; index < UART_SERVICE_CHANNEL_COUNT; ++index)
  {
    channels[index].uart = handles[index];
    if ((byte_ring_init(&channels[index].rx_ring,
                        rx_storage[index],
                        sizeof(rx_storage[index])) != BYTE_RING_OK) ||
        (byte_ring_init(&channels[index].tx_ring,
                        tx_storage[index],
                        sizeof(tx_storage[index])) != BYTE_RING_OK))
    {
      return UART_SERVICE_INVALID_ARGUMENT;
    }
    channels[index].started = 1U;
    hal_status = HAL_UARTEx_ReceiveToIdle_DMA(handles[index],
                                             rx_dma[index],
                                             UART_SERVICE_RX_DMA_CAPACITY);
    if (hal_status != HAL_OK)
    {
      channels[index].started = 0U;
      channels[index].counters.dma_errors = 1U;
      mark_degraded(&channels[index]);
      return UART_SERVICE_HAL_ERROR;
    }
  }
  service_started = 1U;
  return UART_SERVICE_OK;
}

uart_service_status_t uart_service_process(uint32_t event_bits)
{
  uint32_t pending;
  uint32_t selected;
  uint32_t remaining = 0U;
  uint8_t index;
  uint8_t quanta;
  uart_service_status_t result = UART_SERVICE_OK;

  if (service_started == 0U)
  {
    return UART_SERVICE_NOT_STARTED;
  }
  service_counters.process_calls =
    add_u32(service_counters.process_calls, 1U);
  taskENTER_CRITICAL();
  pending = task_pending_bits |
            (event_bits & UART_SERVICE_ALL_CHANNEL_BITS);
  task_pending_bits = 0U;
  taskEXIT_CRITICAL();
  if (pending == 0U)
  {
    service_counters.empty_process_calls =
      add_u32(service_counters.empty_process_calls, 1U);
  }

  for (quanta = 0U; quanta < UART_SERVICE_CHANNEL_COUNT; ++quanta)
  {
    selected = uart_round_robin_take(pending,
                                     UART_SERVICE_CHANNEL_COUNT,
                                     &round_robin_cursor);
    if (selected == 0U)
    {
      break;
    }
    pending &= ~selected;
    for (index = 0U; index < UART_SERVICE_CHANNEL_COUNT; ++index)
    {
      if (selected == (UINT32_C(1) << index))
      {
        if (service_one(index) != UART_SERVICE_OK)
        {
          result = UART_SERVICE_HAL_ERROR;
        }
        service_counters.serviced_quanta =
          add_u32(service_counters.serviced_quanta, 1U);
        if (channel_has_task_work(&channels[index]) != 0U)
        {
          remaining |= selected;
        }
        break;
      }
    }
  }

  taskENTER_CRITICAL();
  task_pending_bits |= remaining;
  taskEXIT_CRITICAL();
  service_counters.next_channel = round_robin_cursor;
#if (INCLUDE_uxTaskGetStackHighWaterMark == 1)
  if ((service_counters.process_calls & UINT32_C(0xFF)) == 0U)
  {
    UBaseType_t stack_words = uxTaskGetStackHighWaterMark(NULL);
    if ((service_counters.task_stack_min_words == 0U) ||
        (stack_words < service_counters.task_stack_min_words))
    {
      service_counters.task_stack_min_words = (uint32_t)stack_words;
    }
  }
#endif
  if ((remaining != 0U) && (owner_task != NULL))
  {
    if (xTaskNotify(owner_task, remaining, eSetBits) == pdPASS)
    {
      service_counters.requeued_notifications =
        add_u32(service_counters.requeued_notifications, 1U);
    }
  }
  return result;
}

size_t uart_service_rx_count(uint8_t logical_channel)
{
  size_t count = 0U;

  if ((service_started != 0U) &&
      (logical_channel >= 1U) &&
      (logical_channel <= UART_SERVICE_CHANNEL_COUNT))
  {
    taskENTER_CRITICAL();
    count = byte_ring_count(&channels[logical_channel - 1U].rx_ring);
    taskEXIT_CRITICAL();
  }
  return count;
}

size_t uart_service_rx_peek(uint8_t logical_channel,
                            uint8_t *data,
                            size_t capacity)
{
  size_t copied = 0U;

  if ((service_started != 0U) &&
      (logical_channel >= 1U) &&
      (logical_channel <= UART_SERVICE_CHANNEL_COUNT) &&
      (data != NULL))
  {
    taskENTER_CRITICAL();
    copied = byte_ring_peek(&channels[logical_channel - 1U].rx_ring,
                            data,
                            capacity);
    taskEXIT_CRITICAL();
  }
  return copied;
}

uart_service_status_t uart_service_rx_discard(uint8_t logical_channel,
                                              size_t length)
{
  size_t discarded;

  if ((service_started == 0U) ||
      (logical_channel < 1U) ||
      (logical_channel > UART_SERVICE_CHANNEL_COUNT))
  {
    return UART_SERVICE_NOT_STARTED;
  }
  taskENTER_CRITICAL();
  discarded = byte_ring_discard(&channels[logical_channel - 1U].rx_ring,
                                length);
  taskEXIT_CRITICAL();
  return (discarded == length) ? UART_SERVICE_OK :
                                 UART_SERVICE_INVALID_ARGUMENT;
}

size_t uart_service_tx_free(uint8_t logical_channel)
{
  size_t free_bytes = 0U;

  if ((service_started != 0U) &&
      (logical_channel >= 1U) &&
      (logical_channel <= UART_SERVICE_CHANNEL_COUNT))
  {
    taskENTER_CRITICAL();
    free_bytes = byte_ring_free(&channels[logical_channel - 1U].tx_ring);
    taskEXIT_CRITICAL();
  }
  return free_bytes;
}

uart_service_status_t uart_service_tx_write(uint8_t logical_channel,
                                            const uint8_t *data,
                                            size_t length)
{
  uart_channel_t *channel;
  byte_ring_status_t ring_status;
  uint32_t bit;

  if ((service_started == 0U) ||
      (logical_channel < 1U) ||
      (logical_channel > UART_SERVICE_CHANNEL_COUNT))
  {
    return UART_SERVICE_NOT_STARTED;
  }
  if ((data == NULL) && (length != 0U))
  {
    return UART_SERVICE_INVALID_ARGUMENT;
  }

  channel = &channels[logical_channel - 1U];
  taskENTER_CRITICAL();
  ring_status = byte_ring_write(&channel->tx_ring, data, length);
  if (ring_status == BYTE_RING_OK)
  {
    channel->counters.tx_bytes_queued =
      add_u32(channel->counters.tx_bytes_queued, (uint32_t)length);
  }
  account_ring_counters(channel);
  taskEXIT_CRITICAL();
  if (ring_status != BYTE_RING_OK)
  {
    mark_degraded(channel);
    return UART_SERVICE_HAL_ERROR;
  }

  bit = UINT32_C(1) << (logical_channel - 1U);
  taskENTER_CRITICAL();
  task_pending_bits |= bit;
  taskEXIT_CRITICAL();
  if ((owner_task != NULL) &&
      (xTaskNotify(owner_task, bit, eSetBits) != pdPASS))
  {
    /* Data is already committed to the ring. task_pending_bits guarantees
     * eventual service, so do not report a transactional write failure. */
    mark_degraded(channel);
  }
  return UART_SERVICE_OK;
}

void uart_service_get_channel_counters(
  uint8_t logical_channel,
  uart_service_channel_counters_t *counters)
{
  if ((logical_channel >= 1U) &&
      (logical_channel <= UART_SERVICE_CHANNEL_COUNT) &&
      (counters != NULL))
  {
    taskENTER_CRITICAL();
    (void)memcpy(counters,
                 (const void *)&channels[logical_channel - 1U].counters,
                 sizeof(*counters));
    taskEXIT_CRITICAL();
  }
}

void uart_service_get_counters(uart_service_counters_t *counters)
{
  if (counters != NULL)
  {
    taskENTER_CRITICAL();
    (void)memcpy(counters,
                 (const void *)&service_counters,
                 sizeof(*counters));
    taskEXIT_CRITICAL();
  }
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
  uart_channel_t *channel;
  uint8_t index;
  HAL_UART_RxEventTypeTypeDef event_type;

  channel = find_channel(huart, &index);
  if ((channel == NULL) || (channel->started == 0U))
  {
    service_counters.unexpected_callbacks =
      add_u32(service_counters.unexpected_callbacks, 1U);
    return;
  }

  event_type = HAL_UARTEx_GetRxEventType(huart);
  if (event_type == HAL_UART_RXEVENT_IDLE)
  {
    channel->counters.rx_idle_events =
      add_u32(channel->counters.rx_idle_events, 1U);
  }
  else if (event_type == HAL_UART_RXEVENT_HT)
  {
    channel->counters.rx_half_events =
      add_u32(channel->counters.rx_half_events, 1U);
  }
  else
  {
    channel->counters.rx_full_events =
      add_u32(channel->counters.rx_full_events, 1U);
  }
  publish_rx_event_from_isr(channel, index, size);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  uart_channel_t *channel;
  uint8_t index;

  channel = find_channel(huart, &index);
  if ((channel == NULL) || (channel->started == 0U))
  {
    service_counters.unexpected_callbacks =
      add_u32(service_counters.unexpected_callbacks, 1U);
    return;
  }

  channel->counters.tx_bytes_dma =
    add_u32(channel->counters.tx_bytes_dma, channel->tx_dma_length);
  channel->counters.tx_complete_events =
    add_u32(channel->counters.tx_complete_events, 1U);
  channel->tx_dma_length = 0U;
  channel->tx_active = 0U;
  channel->tx_complete_pending = 1U;
  __DMB();
  notify_owner_from_isr(channel, index);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  uart_channel_t *channel;
  uint8_t index;
  uint32_t error;
  uint32_t remaining;
  uint16_t position;

  channel = find_channel(huart, &index);
  if ((channel == NULL) || (channel->started == 0U))
  {
    service_counters.unexpected_callbacks =
      add_u32(service_counters.unexpected_callbacks, 1U);
    return;
  }

  error = huart->ErrorCode;
  if (huart->hdmarx != NULL)
  {
    remaining = __HAL_DMA_GET_COUNTER(huart->hdmarx);
    if (remaining <= UART_SERVICE_RX_DMA_CAPACITY)
    {
      position = (uint16_t)(UART_SERVICE_RX_DMA_CAPACITY - remaining);
      publish_rx_event_from_isr(channel, index, position);
    }
  }
  if ((error & HAL_UART_ERROR_FE) != 0U)
  {
    channel->counters.framing_errors =
      add_u32(channel->counters.framing_errors, 1U);
  }
  if ((error & HAL_UART_ERROR_NE) != 0U)
  {
    channel->counters.noise_errors =
      add_u32(channel->counters.noise_errors, 1U);
  }
  if ((error & HAL_UART_ERROR_ORE) != 0U)
  {
    channel->counters.overrun_errors =
      add_u32(channel->counters.overrun_errors, 1U);
    mark_data_loss(channel);
  }
  if ((error & HAL_UART_ERROR_DMA) != 0U)
  {
    channel->counters.dma_errors =
      add_u32(channel->counters.dma_errors, 1U);
    if (channel->tx_active != 0U)
    {
      channel->counters.tx_dma_uncertain_bytes =
        add_u32(channel->counters.tx_dma_uncertain_bytes,
                channel->tx_dma_length);
      channel->tx_dma_length = 0U;
      channel->tx_active = 0U;
      mark_data_loss(channel);
    }
  }
  mark_degraded(channel);
  channel->rx_restart_pending = 1U;
  notify_owner_from_isr(channel, index);
}
