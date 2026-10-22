#include "platform_supervisor.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "platform_supervisor_port.h"

typedef struct
{
  platform_supervisor_snapshot_t snapshot;
  uint32_t initialized_ms;
} supervisor_state_t;

static supervisor_state_t supervisor_state;

static uint32_t add_u32(uint32_t value, uint32_t increment)
{
  return (increment > (UINT32_MAX - value)) ? UINT32_MAX : value + increment;
}

void platform_supervisor_init(uint32_t now_ms)
{
  (void)memset(&supervisor_state, 0, sizeof(supervisor_state));
  supervisor_state.initialized_ms = now_ms;
  supervisor_state.snapshot.health = PLATFORM_HEALTH_STARTING;
  supervisor_state.snapshot.tasks[PLATFORM_SUPERVISOR_TASK_APP].timeout_ms =
    100U;
  supervisor_state.snapshot.tasks[PLATFORM_SUPERVISOR_TASK_TCPIP].timeout_ms =
    750U;
  supervisor_state.snapshot.tasks[PLATFORM_SUPERVISOR_TASK_ETH_LINK].timeout_ms =
    500U;
  /* IWDG is intentionally disabled until all three target heartbeats pass HIL. */
  supervisor_state.snapshot.iwdg_enabled = 0U;
}

void platform_supervisor_heartbeat(platform_supervisor_task_t task,
                                   uint32_t now_ms,
                                   uint32_t stack_free_words)
{
  platform_supervisor_task_snapshot_t *task_state;

  if ((uint32_t)task >= PLATFORM_SUPERVISOR_TASK_COUNT)
  {
    return;
  }
  task_state = &supervisor_state.snapshot.tasks[(uint32_t)task];
  PLATFORM_SUPERVISOR_LOCK();
  task_state->last_heartbeat_ms = now_ms;
  task_state->heartbeat_count = add_u32(task_state->heartbeat_count, 1U);
  if ((stack_free_words != 0U) &&
      ((task_state->stack_min_words == 0U) ||
       (stack_free_words < task_state->stack_min_words)))
  {
    task_state->stack_min_words = stack_free_words;
  }
  PLATFORM_SUPERVISOR_UNLOCK();
}

void platform_supervisor_set_network(uint8_t link_up,
                                     uint8_t peer_session_valid,
                                     uint32_t local_session_id,
                                     uint32_t peer_session_id)
{
  PLATFORM_SUPERVISOR_LOCK();
  supervisor_state.snapshot.link_up = (link_up != 0U) ? 1U : 0U;
  supervisor_state.snapshot.peer_session_valid =
    (peer_session_valid != 0U) ? 1U : 0U;
  supervisor_state.snapshot.local_session_id = local_session_id;
  supervisor_state.snapshot.peer_session_id = peer_session_id;
  PLATFORM_SUPERVISOR_UNLOCK();
}

platform_health_t platform_supervisor_evaluate(uint32_t now_ms)
{
  platform_health_t previous;
  uint32_t stale_mask = 0U;
  uint8_t index;
  uint8_t all_seen = 1U;

  PLATFORM_SUPERVISOR_LOCK();
  previous = supervisor_state.snapshot.health;
  for (index = 0U; index < PLATFORM_SUPERVISOR_TASK_COUNT; ++index)
  {
    const platform_supervisor_task_snapshot_t *task =
      &supervisor_state.snapshot.tasks[index];
    if (task->heartbeat_count == 0U)
    {
      all_seen = 0U;
      if ((uint32_t)(now_ms - supervisor_state.initialized_ms) >=
          PLATFORM_SUPERVISOR_STARTUP_GRACE_MS)
      {
        stale_mask |= UINT32_C(1) << index;
      }
    }
    else if ((uint32_t)(now_ms - task->last_heartbeat_ms) > task->timeout_ms)
    {
      stale_mask |= UINT32_C(1) << index;
    }
  }
  supervisor_state.snapshot.stale_task_mask = stale_mask;
  if (stale_mask != 0U)
  {
    supervisor_state.snapshot.health = PLATFORM_HEALTH_FAULT_TASK;
  }
  else if (all_seen == 0U)
  {
    supervisor_state.snapshot.health = PLATFORM_HEALTH_STARTING;
  }
  else if (supervisor_state.snapshot.link_up == 0U)
  {
    supervisor_state.snapshot.health = PLATFORM_HEALTH_DEGRADED_LINK;
  }
  else if (supervisor_state.snapshot.peer_session_valid == 0U)
  {
    supervisor_state.snapshot.health = PLATFORM_HEALTH_DEGRADED_SESSION;
  }
  else
  {
    supervisor_state.snapshot.health = PLATFORM_HEALTH_HEALTHY;
  }
  supervisor_state.snapshot.evaluation_count =
    add_u32(supervisor_state.snapshot.evaluation_count, 1U);
  if ((previous != PLATFORM_HEALTH_FAULT_TASK) &&
      (supervisor_state.snapshot.health == PLATFORM_HEALTH_FAULT_TASK))
  {
    supervisor_state.snapshot.fault_events =
      add_u32(supervisor_state.snapshot.fault_events, 1U);
  }
  else if ((previous == PLATFORM_HEALTH_FAULT_TASK) &&
           (supervisor_state.snapshot.health != PLATFORM_HEALTH_FAULT_TASK))
  {
    supervisor_state.snapshot.recovery_events =
      add_u32(supervisor_state.snapshot.recovery_events, 1U);
  }
  PLATFORM_SUPERVISOR_UNLOCK();
  return supervisor_state.snapshot.health;
}

void platform_supervisor_get_snapshot(platform_supervisor_snapshot_t *out)
{
  if (out != NULL)
  {
    PLATFORM_SUPERVISOR_LOCK();
    (void)memcpy(out, &supervisor_state.snapshot, sizeof(*out));
    PLATFORM_SUPERVISOR_UNLOCK();
  }
}
