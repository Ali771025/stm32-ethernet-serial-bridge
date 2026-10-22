#include "u8b_backpressure.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

static uint32_t add_u32(uint32_t value, uint32_t increment)
{
  return (increment > (UINT32_MAX - value)) ? UINT32_MAX : value + increment;
}

uint8_t u8b_backpressure_allow_queue(uint8_t tx_window_count,
                                     uint32_t total_pending,
                                     uint32_t maximum_channel_pending,
                                     uint8_t normal_flush_due)
{
  if (total_pending == 0U)
  {
    return 0U;
  }
  if (tx_window_count == 0U)
  {
    return normal_flush_due;
  }
  if ((maximum_channel_pending >=
       U8B_BACKPRESSURE_EFFICIENT_SLOT_BYTES) ||
      (total_pending >= U8B_BACKPRESSURE_FULL_PAYLOAD_DATA))
  {
    return 1U;
  }
  return 0U;
}

uint32_t u8b_backpressure_guaranteed_outage_ms(void)
{
  return (U8B_BACKPRESSURE_TOTAL_GUARANTEED_BYTES * UINT32_C(1000)) /
         U8B_BACKPRESSURE_AGGREGATE_BYTES_PER_SECOND;
}

uint8_t u8b_backpressure_configuration_fits(void)
{
  return (U8B_BACKPRESSURE_CONFIGURED_OUTAGE_MS <=
          u8b_backpressure_guaranteed_outage_ms()) ? 1U : 0U;
}

void u8b_backpressure_monitor_init(u8b_backpressure_monitor_t *monitor)
{
  if (monitor != NULL)
  {
    (void)memset(monitor, 0, sizeof(*monitor));
  }
}

void u8b_backpressure_monitor_update(u8b_backpressure_monitor_t *monitor,
                                     uint32_t now_ms,
                                     uint8_t pressure_active)
{
  uint32_t duration;

  if (monitor == NULL)
  {
    return;
  }
  if (pressure_active != 0U)
  {
    if (monitor->active == 0U)
    {
      monitor->active = 1U;
      monitor->pressure_started_ms = now_ms;
      monitor->current_duration_ms = 0U;
      monitor->pressure_events = add_u32(monitor->pressure_events, 1U);
    }
    duration = now_ms - monitor->pressure_started_ms;
    monitor->current_duration_ms = duration;
    if (duration > monitor->maximum_duration_ms)
    {
      monitor->maximum_duration_ms = duration;
    }
  }
  else if (monitor->active != 0U)
  {
    monitor->active = 0U;
    monitor->current_duration_ms = 0U;
    monitor->recovery_events = add_u32(monitor->recovery_events, 1U);
  }
}
