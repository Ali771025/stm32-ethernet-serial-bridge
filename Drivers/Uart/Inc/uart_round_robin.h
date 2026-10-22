#ifndef UART_ROUND_ROBIN_H
#define UART_ROUND_ROBIN_H

#include <stdint.h>

/*
 * Pure C helper shared by target code and host tests. For a non-zero pending
 * bitmap it selects the first ready channel at/after *cursor, wrapping once,
 * then advances *cursor past that channel. channel_count must be 1..32.
 */
uint32_t uart_round_robin_take(uint32_t pending,
                               uint8_t channel_count,
                               uint8_t *cursor);

#endif /* UART_ROUND_ROBIN_H */
