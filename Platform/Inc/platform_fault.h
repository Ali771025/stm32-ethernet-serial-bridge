#ifndef PLATFORM_FAULT_H
#define PLATFORM_FAULT_H

#include <stdint.h>

typedef enum
{
  PLATFORM_FAULT_NONE = 0,
  PLATFORM_FAULT_FREERTOS_ASSERT = 1,
  PLATFORM_FAULT_STACK_OVERFLOW = 2,
  PLATFORM_FAULT_MALLOC_FAILED = 3,
  PLATFORM_FAULT_KERNEL_INIT = 4,
  PLATFORM_FAULT_TASK_CREATE = 5,
  PLATFORM_FAULT_KERNEL_START = 6,
  PLATFORM_FAULT_UART_SERVICE_START = 7,
  PLATFORM_FAULT_NETWORK_INIT = 8,
  PLATFORM_FAULT_NET_DIAG_INIT = 9,
  PLATFORM_FAULT_NET_BRIDGE_INIT = 10,
  PLATFORM_FAULT_HARDFAULT = 11,
  PLATFORM_FAULT_HAL_ERROR = 12,
  PLATFORM_FAULT_SUPERVISOR_STALE = 13
} platform_fault_reason_t;

typedef struct
{
  platform_fault_reason_t reason;
  const char *file;
  uint32_t line;
} platform_fault_record_t;

/* Debugger-observable first-fault record. Written once, then execution stops. */
extern volatile platform_fault_record_t platform_fault_record;

void platform_fault_raise(platform_fault_reason_t reason,
                          const char *file,
                          uint32_t line);

/* Kept declaration-only in FreeRTOSConfig.h so the assembly port can parse it. */
void platform_freertos_assert_failed(const char *file, uint32_t line);

#endif /* PLATFORM_FAULT_H */
