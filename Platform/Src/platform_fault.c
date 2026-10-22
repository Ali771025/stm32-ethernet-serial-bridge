#include "platform_fault.h"

#include "stm32h7xx.h"
#include "platform_reset.h"

volatile platform_fault_record_t platform_fault_record = {
  PLATFORM_FAULT_NONE,
  (const char *)0,
  0U
};

void platform_fault_raise(platform_fault_reason_t reason,
                          const char *file,
                          uint32_t line)
{
  __disable_irq();

  if (platform_fault_record.reason == PLATFORM_FAULT_NONE)
  {
    platform_fault_record.reason = reason;
    platform_fault_record.file = file;
    platform_fault_record.line = line;
    platform_reset_note_fault((uint32_t)reason);
  }

  __DSB();
  __ISB();
  for (;;)
  {
    __NOP();
  }
}

void platform_freertos_assert_failed(const char *file, uint32_t line)
{
  platform_fault_raise(PLATFORM_FAULT_FREERTOS_ASSERT, file, line);
}
