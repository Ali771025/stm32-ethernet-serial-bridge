#ifndef PLATFORM_SUPERVISOR_H
#define PLATFORM_SUPERVISOR_H

#include <stdint.h>

#define PLATFORM_SUPERVISOR_TASK_COUNT 3U
#define PLATFORM_SUPERVISOR_ALL_TASKS UINT32_C(0x00000007)
#define PLATFORM_SUPERVISOR_STARTUP_GRACE_MS 2000U

typedef enum
{
  PLATFORM_SUPERVISOR_TASK_APP = 0,
  PLATFORM_SUPERVISOR_TASK_TCPIP = 1,
  PLATFORM_SUPERVISOR_TASK_ETH_LINK = 2
} platform_supervisor_task_t;

typedef enum
{
  PLATFORM_HEALTH_STARTING = 0,
  PLATFORM_HEALTH_HEALTHY = 1,
  PLATFORM_HEALTH_DEGRADED_LINK = 2,
  PLATFORM_HEALTH_DEGRADED_SESSION = 3,
  PLATFORM_HEALTH_FAULT_TASK = 4
} platform_health_t;

typedef struct
{
  uint32_t heartbeat_count;
  uint32_t last_heartbeat_ms;
  uint32_t timeout_ms;
  uint32_t stack_min_words;
} platform_supervisor_task_snapshot_t;

typedef struct
{
  platform_supervisor_task_snapshot_t tasks[PLATFORM_SUPERVISOR_TASK_COUNT];
  uint32_t evaluation_count;
  uint32_t fault_events;
  uint32_t recovery_events;
  uint32_t stale_task_mask;
  uint32_t local_session_id;
  uint32_t peer_session_id;
  platform_health_t health;
  uint8_t link_up;
  uint8_t peer_session_valid;
  uint8_t iwdg_enabled;
} platform_supervisor_snapshot_t;

void platform_supervisor_init(uint32_t now_ms);
void platform_supervisor_heartbeat(platform_supervisor_task_t task,
                                   uint32_t now_ms,
                                   uint32_t stack_free_words);
void platform_supervisor_set_network(uint8_t link_up,
                                     uint8_t peer_session_valid,
                                     uint32_t local_session_id,
                                     uint32_t peer_session_id);
platform_health_t platform_supervisor_evaluate(uint32_t now_ms);
void platform_supervisor_get_snapshot(platform_supervisor_snapshot_t *out);

#endif /* PLATFORM_SUPERVISOR_H */
