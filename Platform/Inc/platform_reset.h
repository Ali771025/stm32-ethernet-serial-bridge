#ifndef PLATFORM_RESET_H
#define PLATFORM_RESET_H

#include <stdint.h>

#define PLATFORM_RESET_FLAG_CPU      UINT32_C(0x00000001)
#define PLATFORM_RESET_FLAG_D1       UINT32_C(0x00000002)
#define PLATFORM_RESET_FLAG_D2       UINT32_C(0x00000004)
#define PLATFORM_RESET_FLAG_BROWNOUT UINT32_C(0x00000008)
#define PLATFORM_RESET_FLAG_PIN      UINT32_C(0x00000010)
#define PLATFORM_RESET_FLAG_POWER_ON UINT32_C(0x00000020)
#define PLATFORM_RESET_FLAG_SOFTWARE UINT32_C(0x00000040)
#define PLATFORM_RESET_FLAG_IWDG     UINT32_C(0x00000080)
#define PLATFORM_RESET_FLAG_WWDG     UINT32_C(0x00000100)
#define PLATFORM_RESET_FLAG_LOWPOWER UINT32_C(0x00000200)

typedef enum
{
  PLATFORM_RESET_UNKNOWN = 0,
  PLATFORM_RESET_POWER_ON = 1,
  PLATFORM_RESET_BROWNOUT = 2,
  PLATFORM_RESET_PIN = 3,
  PLATFORM_RESET_SOFTWARE = 4,
  PLATFORM_RESET_IWDG = 5,
  PLATFORM_RESET_WWDG = 6,
  PLATFORM_RESET_LOWPOWER = 7,
  PLATFORM_RESET_CPU = 8
} platform_reset_reason_t;

typedef struct
{
  uint32_t boot_count;
  uint32_t reset_flags;
  uint32_t last_fault_reason;
  uint32_t last_stale_task_mask;
  uint32_t last_health;
  platform_reset_reason_t reset_reason;
  uint8_t retained_valid;
} platform_reset_snapshot_t;

/* Capture and clear STM32 RCC reset flags once, immediately after HAL_Init. */
void platform_reset_boot_capture(void);
platform_reset_reason_t platform_reset_classify(uint32_t flags);
void platform_reset_note_fault(uint32_t fault_reason);
void platform_reset_note_supervisor(uint32_t stale_task_mask,
                                    uint32_t health);
void platform_reset_get_snapshot(platform_reset_snapshot_t *out);

#endif /* PLATFORM_RESET_H */
