#ifndef PLATFORM_SESSION_H
#define PLATFORM_SESSION_H

#include <stdint.h>

/*
 * Returns a non-zero boot session identifier. A retained no-init counter makes
 * warm resets distinct; cold-start uniqueness also incorporates the STM32 UID.
 * This identifier detects stale traffic but is not a cryptographic nonce.
 */
uint32_t platform_session_next(void);

#endif /* PLATFORM_SESSION_H */
