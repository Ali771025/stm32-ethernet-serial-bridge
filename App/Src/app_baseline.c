#include "app_baseline.h"

#include "board_config.h"

volatile uint32_t app_baseline_heartbeat_count;

static uint32_t heartbeat_deadline_ms;
static const board_config_t *active_board_config;

void app_baseline_init(uint32_t now_ms)
{
  active_board_config = board_config_get();
  app_baseline_heartbeat_count = 0U;
  heartbeat_deadline_ms = now_ms + APP_BASELINE_HEARTBEAT_PERIOD_MS;

  /* Keep the identity object live for debugger inspection in this baseline. */
  (void)active_board_config;
}

void app_baseline_poll(uint32_t now_ms)
{
  if ((int32_t)(now_ms - heartbeat_deadline_ms) >= 0)
  {
    heartbeat_deadline_ms += APP_BASELINE_HEARTBEAT_PERIOD_MS;
    app_baseline_heartbeat_count++;
  }
}
