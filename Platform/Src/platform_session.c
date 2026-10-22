#include "platform_session.h"

#include "stm32h7xx_hal.h"

#define SESSION_RETAINED_MAGIC UINT32_C(0x55384253)

typedef struct
{
  uint32_t magic;
  uint32_t counter;
  uint32_t counter_inverse;
  uint32_t last_session;
} session_retained_t;

#if defined(__ICCARM__)
#pragma location = ".noinit"
__no_init static session_retained_t retained_session;
#elif defined(__GNUC__)
static session_retained_t retained_session
  __attribute__((section(".noinit")));
#else
static session_retained_t retained_session;
#endif

static uint32_t mix_word(uint32_t hash, uint32_t word)
{
  uint8_t shift;

  for (shift = 0U; shift < 32U; shift += 8U)
  {
    hash ^= (word >> shift) & UINT32_C(0xFF);
    hash *= UINT32_C(16777619);
  }
  return hash;
}

uint32_t platform_session_next(void)
{
  uint32_t counter;
  uint32_t session;
  uint32_t retained_noise = retained_session.last_session;

  if ((retained_session.magic == SESSION_RETAINED_MAGIC) &&
      (retained_session.counter != 0U) &&
      (retained_session.counter_inverse == ~retained_session.counter))
  {
    counter = retained_session.counter + 1U;
    if (counter == 0U)
    {
      counter = 1U;
    }
  }
  else
  {
    counter = 1U;
    retained_noise ^= retained_session.magic;
    retained_noise ^= retained_session.counter;
    retained_noise ^= retained_session.counter_inverse;
  }

  session = UINT32_C(2166136261);
  session = mix_word(session, HAL_GetUIDw0());
  session = mix_word(session, HAL_GetUIDw1());
  session = mix_word(session, HAL_GetUIDw2());
  session = mix_word(session, counter);
  session = mix_word(session, retained_noise);
  session = mix_word(session, SysTick->VAL);
  if (session == 0U)
  {
    session = UINT32_C(1);
  }

  retained_session.magic = SESSION_RETAINED_MAGIC;
  retained_session.counter = counter;
  retained_session.counter_inverse = ~counter;
  retained_session.last_session = session;
  __DMB();
  return session;
}
