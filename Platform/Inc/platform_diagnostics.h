#ifndef PLATFORM_DIAGNOSTICS_H
#define PLATFORM_DIAGNOSTICS_H

#include <stddef.h>
#include <stdint.h>

#include "platform_reset.h"
#include "platform_supervisor.h"

#define PLATFORM_DIAG_REQUEST_SIZE 8U
#define PLATFORM_DIAG_RESPONSE_MAX 512U
#define PLATFORM_DIAG_NETWORK_COUNTERS 16U
#define PLATFORM_DIAG_UART_GLOBAL_COUNTERS 4U
#define PLATFORM_DIAG_CHANNEL_COUNT 8U
#define PLATFORM_DIAG_CHANNEL_COUNTERS 8U
#define PLATFORM_DIAG_RATE_LIMIT_MS 100U

typedef enum
{
  PLATFORM_DIAG_OK = 0,
  PLATFORM_DIAG_INVALID_ARGUMENT,
  PLATFORM_DIAG_BAD_REQUEST,
  PLATFORM_DIAG_NO_SPACE
} platform_diag_status_t;

typedef struct
{
  platform_supervisor_snapshot_t supervisor;
  platform_reset_snapshot_t reset;
  uint32_t sequence;
  uint32_t uptime_ms;
  uint32_t network[PLATFORM_DIAG_NETWORK_COUNTERS];
  uint32_t uart_global[PLATFORM_DIAG_UART_GLOBAL_COUNTERS];
  uint32_t channel[PLATFORM_DIAG_CHANNEL_COUNT]
                  [PLATFORM_DIAG_CHANNEL_COUNTERS];
} platform_diag_snapshot_t;

platform_diag_status_t platform_diag_validate_request(const uint8_t *request,
                                                      size_t length);
platform_diag_status_t platform_diag_encode_snapshot(
  const platform_diag_snapshot_t *snapshot,
  uint8_t *output,
  size_t capacity,
  size_t *length);
uint8_t platform_diag_rate_allowed(uint32_t now_ms,
                                   uint32_t last_response_ms,
                                   uint8_t response_was_sent);

#endif /* PLATFORM_DIAGNOSTICS_H */
