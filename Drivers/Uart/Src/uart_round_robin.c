#include "uart_round_robin.h"

#include <stddef.h>

uint32_t uart_round_robin_take(uint32_t pending,
                               uint8_t channel_count,
                               uint8_t *cursor)
{
  uint8_t offset;
  uint8_t channel;
  uint32_t bit;

  if ((cursor == NULL) || (channel_count == 0U) || (channel_count > 32U))
  {
    return 0U;
  }
  if (*cursor >= channel_count)
  {
    *cursor = 0U;
  }

  for (offset = 0U; offset < channel_count; ++offset)
  {
    channel = (uint8_t)((*cursor + offset) % channel_count);
    bit = UINT32_C(1) << channel;
    if ((pending & bit) != 0U)
    {
      *cursor = (uint8_t)((channel + 1U) % channel_count);
      return bit;
    }
  }
  return 0U;
}
