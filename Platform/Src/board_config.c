#include "board_config.h"

#if (U8B_BOARD_ROLE == U8B_BOARD_ROLE_A)
static const board_config_t board_config = {
  U8B_BOARD_ROLE_A,
  {192U, 168U, 127U, 1U},
  {192U, 168U, 127U, 2U},
  {255U, 255U, 255U, 0U},
  40000U
};
#else
static const board_config_t board_config = {
  U8B_BOARD_ROLE_B,
  {192U, 168U, 127U, 2U},
  {192U, 168U, 127U, 1U},
  {255U, 255U, 255U, 0U},
  40000U
};
#endif

const board_config_t *board_config_get(void)
{
  return &board_config;
}
