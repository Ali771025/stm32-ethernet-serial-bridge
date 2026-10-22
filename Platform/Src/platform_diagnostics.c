#include "platform_diagnostics.h"

#include <stddef.h>

static void write_be16(uint8_t *output, uint16_t value)
{
  output[0] = (uint8_t)(value >> 8);
  output[1] = (uint8_t)value;
}

static void write_be32(uint8_t *output, uint32_t value)
{
  output[0] = (uint8_t)(value >> 24);
  output[1] = (uint8_t)(value >> 16);
  output[2] = (uint8_t)(value >> 8);
  output[3] = (uint8_t)value;
}

static uint8_t append_u32(uint8_t *output,
                          size_t capacity,
                          size_t *offset,
                          uint32_t value)
{
  if ((capacity - *offset) < 4U)
  {
    return 0U;
  }
  write_be32(&output[*offset], value);
  *offset += 4U;
  return 1U;
}

platform_diag_status_t platform_diag_validate_request(const uint8_t *request,
                                                      size_t length)
{
  static const uint8_t expected[PLATFORM_DIAG_REQUEST_SIZE] =
  {
    0x55U, 0x38U, 0x44U, 0x31U, 0x01U, 0x01U, 0x00U, 0x00U
  };
  size_t index;

  if (request == NULL)
  {
    return PLATFORM_DIAG_INVALID_ARGUMENT;
  }
  if (length != PLATFORM_DIAG_REQUEST_SIZE)
  {
    return PLATFORM_DIAG_BAD_REQUEST;
  }
  for (index = 0U; index < PLATFORM_DIAG_REQUEST_SIZE; ++index)
  {
    if (request[index] != expected[index])
    {
      return PLATFORM_DIAG_BAD_REQUEST;
    }
  }
  return PLATFORM_DIAG_OK;
}

platform_diag_status_t platform_diag_encode_snapshot(
  const platform_diag_snapshot_t *snapshot,
  uint8_t *output,
  size_t capacity,
  size_t *length)
{
  size_t offset = 8U;
  uint8_t task;
  uint8_t index;
  uint8_t channel;

  if ((snapshot == NULL) || (output == NULL) || (length == NULL))
  {
    return PLATFORM_DIAG_INVALID_ARGUMENT;
  }
  if (capacity < 8U)
  {
    return PLATFORM_DIAG_NO_SPACE;
  }
  output[0] = 0x55U;
  output[1] = 0x38U;
  output[2] = 0x44U;
  output[3] = 0x31U;
  output[4] = 0x01U;
  output[5] = 0x81U;
  output[6] = 0U;
  output[7] = 0U;

#define APPEND(value) do { \
  if (append_u32(output, capacity, &offset, (uint32_t)(value)) == 0U) { \
    return PLATFORM_DIAG_NO_SPACE; \
  } \
} while (0)

  APPEND(snapshot->sequence);
  APPEND(snapshot->uptime_ms);
  APPEND(snapshot->supervisor.health);
  APPEND(snapshot->supervisor.stale_task_mask);
  APPEND(snapshot->supervisor.link_up);
  APPEND(snapshot->supervisor.peer_session_valid);
  APPEND(snapshot->supervisor.iwdg_enabled);
  APPEND(snapshot->supervisor.local_session_id);
  APPEND(snapshot->supervisor.peer_session_id);
  APPEND(snapshot->supervisor.fault_events);
  APPEND(snapshot->supervisor.recovery_events);
  APPEND(snapshot->reset.boot_count);
  APPEND(snapshot->reset.reset_flags);
  APPEND(snapshot->reset.reset_reason);
  APPEND(snapshot->reset.last_fault_reason);
  APPEND(snapshot->reset.last_stale_task_mask);
  APPEND(snapshot->reset.last_health);
  for (task = 0U; task < PLATFORM_SUPERVISOR_TASK_COUNT; ++task)
  {
    APPEND(snapshot->supervisor.tasks[task].heartbeat_count);
    APPEND(snapshot->supervisor.tasks[task].last_heartbeat_ms);
    APPEND(snapshot->supervisor.tasks[task].stack_min_words);
  }
  for (index = 0U; index < PLATFORM_DIAG_NETWORK_COUNTERS; ++index)
  {
    APPEND(snapshot->network[index]);
  }
  for (index = 0U; index < PLATFORM_DIAG_UART_GLOBAL_COUNTERS; ++index)
  {
    APPEND(snapshot->uart_global[index]);
  }
  for (channel = 0U; channel < PLATFORM_DIAG_CHANNEL_COUNT; ++channel)
  {
    for (index = 0U; index < PLATFORM_DIAG_CHANNEL_COUNTERS; ++index)
    {
      APPEND(snapshot->channel[channel][index]);
    }
  }
#undef APPEND

  if (offset > UINT16_MAX)
  {
    return PLATFORM_DIAG_NO_SPACE;
  }
  write_be16(&output[6], (uint16_t)offset);
  *length = offset;
  return PLATFORM_DIAG_OK;
}

uint8_t platform_diag_rate_allowed(uint32_t now_ms,
                                   uint32_t last_response_ms,
                                   uint8_t response_was_sent)
{
  if (response_was_sent == 0U)
  {
    return 1U;
  }
  return ((uint32_t)(now_ms - last_response_ms) >=
          PLATFORM_DIAG_RATE_LIMIT_MS) ? 1U : 0U;
}
