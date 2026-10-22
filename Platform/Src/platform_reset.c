#include "platform_reset.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "stm32h7xx_hal.h"
#include "platform_supervisor_port.h"

#define RESET_RETAINED_MAGIC UINT32_C(0x55385253)
#define RESET_RETAINED_VERSION UINT32_C(1)

typedef struct
{
  uint32_t magic;
  uint32_t version;
  uint32_t boot_count;
  uint32_t reset_flags;
  uint32_t last_fault_reason;
  uint32_t last_stale_task_mask;
  uint32_t last_health;
  uint32_t checksum;
} reset_retained_t;

#if defined(__ICCARM__)
#pragma location = ".noinit"
__no_init static reset_retained_t retained_reset;
#elif defined(__GNUC__)
static reset_retained_t retained_reset __attribute__((section(".noinit")));
#else
static reset_retained_t retained_reset;
#endif

static platform_reset_snapshot_t reset_snapshot;

static uint32_t retained_checksum(const reset_retained_t *record)
{
  return record->magic ^ record->version ^ record->boot_count ^
         record->reset_flags ^ record->last_fault_reason ^
         record->last_stale_task_mask ^ record->last_health ^
         UINT32_C(0xA55AA55A);
}

static uint8_t retained_is_valid(void)
{
  return ((retained_reset.magic == RESET_RETAINED_MAGIC) &&
          (retained_reset.version == RESET_RETAINED_VERSION) &&
          (retained_reset.checksum == retained_checksum(&retained_reset))) ?
         1U : 0U;
}

static void retained_commit(void)
{
  retained_reset.checksum = retained_checksum(&retained_reset);
  __DMB();
}

platform_reset_reason_t platform_reset_classify(uint32_t flags)
{
  if ((flags & PLATFORM_RESET_FLAG_IWDG) != 0U)
  {
    return PLATFORM_RESET_IWDG;
  }
  if ((flags & PLATFORM_RESET_FLAG_WWDG) != 0U)
  {
    return PLATFORM_RESET_WWDG;
  }
  if ((flags & PLATFORM_RESET_FLAG_SOFTWARE) != 0U)
  {
    return PLATFORM_RESET_SOFTWARE;
  }
  if ((flags & PLATFORM_RESET_FLAG_POWER_ON) != 0U)
  {
    return PLATFORM_RESET_POWER_ON;
  }
  if ((flags & PLATFORM_RESET_FLAG_BROWNOUT) != 0U)
  {
    return PLATFORM_RESET_BROWNOUT;
  }
  if ((flags & PLATFORM_RESET_FLAG_PIN) != 0U)
  {
    return PLATFORM_RESET_PIN;
  }
  if ((flags & PLATFORM_RESET_FLAG_LOWPOWER) != 0U)
  {
    return PLATFORM_RESET_LOWPOWER;
  }
  if ((flags & (PLATFORM_RESET_FLAG_CPU | PLATFORM_RESET_FLAG_D1 |
                PLATFORM_RESET_FLAG_D2)) != 0U)
  {
    return PLATFORM_RESET_CPU;
  }
  return PLATFORM_RESET_UNKNOWN;
}

void platform_reset_boot_capture(void)
{
  uint32_t flags = 0U;
  uint8_t valid = retained_is_valid();

  if (__HAL_RCC_GET_FLAG(RCC_FLAG_CPURST) != 0U) { flags |= PLATFORM_RESET_FLAG_CPU; }
  if (__HAL_RCC_GET_FLAG(RCC_FLAG_D1RST) != 0U) { flags |= PLATFORM_RESET_FLAG_D1; }
  if (__HAL_RCC_GET_FLAG(RCC_FLAG_D2RST) != 0U) { flags |= PLATFORM_RESET_FLAG_D2; }
  if (__HAL_RCC_GET_FLAG(RCC_FLAG_BORRST) != 0U) { flags |= PLATFORM_RESET_FLAG_BROWNOUT; }
  if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST) != 0U) { flags |= PLATFORM_RESET_FLAG_PIN; }
  if (__HAL_RCC_GET_FLAG(RCC_FLAG_PORRST) != 0U) { flags |= PLATFORM_RESET_FLAG_POWER_ON; }
  if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST) != 0U) { flags |= PLATFORM_RESET_FLAG_SOFTWARE; }
  if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDG1RST) != 0U) { flags |= PLATFORM_RESET_FLAG_IWDG; }
  if (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDG1RST) != 0U) { flags |= PLATFORM_RESET_FLAG_WWDG; }
  if ((__HAL_RCC_GET_FLAG(RCC_FLAG_LPWR1RST) != 0U) ||
      (__HAL_RCC_GET_FLAG(RCC_FLAG_LPWR2RST) != 0U))
  {
    flags |= PLATFORM_RESET_FLAG_LOWPOWER;
  }
  __HAL_RCC_CLEAR_RESET_FLAGS();

  if (valid == 0U)
  {
    (void)memset(&retained_reset, 0, sizeof(retained_reset));
    retained_reset.magic = RESET_RETAINED_MAGIC;
    retained_reset.version = RESET_RETAINED_VERSION;
  }
  if (retained_reset.boot_count != UINT32_MAX)
  {
    ++retained_reset.boot_count;
  }
  retained_reset.reset_flags = flags;
  retained_commit();

  reset_snapshot.boot_count = retained_reset.boot_count;
  reset_snapshot.reset_flags = flags;
  reset_snapshot.last_fault_reason = retained_reset.last_fault_reason;
  reset_snapshot.last_stale_task_mask = retained_reset.last_stale_task_mask;
  reset_snapshot.last_health = retained_reset.last_health;
  reset_snapshot.reset_reason = platform_reset_classify(flags);
  reset_snapshot.retained_valid = valid;
}

void platform_reset_note_fault(uint32_t fault_reason)
{
  /* platform_fault_raise disables IRQs before calling this path. */
  retained_reset.last_fault_reason = fault_reason;
  retained_commit();
  reset_snapshot.last_fault_reason = fault_reason;
}

void platform_reset_note_supervisor(uint32_t stale_task_mask,
                                    uint32_t health)
{
  PLATFORM_SUPERVISOR_LOCK();
  retained_reset.last_stale_task_mask = stale_task_mask;
  retained_reset.last_health = health;
  retained_commit();
  reset_snapshot.last_stale_task_mask = stale_task_mask;
  reset_snapshot.last_health = health;
  PLATFORM_SUPERVISOR_UNLOCK();
}

void platform_reset_get_snapshot(platform_reset_snapshot_t *out)
{
  if (out != NULL)
  {
    PLATFORM_SUPERVISOR_LOCK();
    (void)memcpy(out, &reset_snapshot, sizeof(*out));
    PLATFORM_SUPERVISOR_UNLOCK();
  }
}
