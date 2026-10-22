#ifndef APP_BASELINE_H
#define APP_BASELINE_H

#include <stdint.h>

#define APP_BASELINE_HEARTBEAT_PERIOD_MS (500U)

/* Debugger-observable heartbeat; incremented once per configured period. */
extern volatile uint32_t app_baseline_heartbeat_count;

void app_baseline_init(uint32_t now_ms);
void app_baseline_poll(uint32_t now_ms);

#endif /* APP_BASELINE_H */
