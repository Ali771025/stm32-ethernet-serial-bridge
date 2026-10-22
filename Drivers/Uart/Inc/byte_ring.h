#ifndef BYTE_RING_H
#define BYTE_RING_H

#include <stddef.h>
#include <stdint.h>

typedef enum
{
  BYTE_RING_OK = 0,
  BYTE_RING_INVALID_ARGUMENT,
  BYTE_RING_NO_SPACE
} byte_ring_status_t;

/*
 * Caller-owned, task-context byte ring. The storage remains owned by the
 * caller for the lifetime of the ring. A write is all-or-nothing: when there
 * is insufficient space, no byte is written and rejection counters advance.
 * This module provides no ISR/task synchronization.
 */
typedef struct
{
  uint8_t *storage;
  size_t capacity;
  size_t head;
  size_t tail;
  size_t count;
  size_t high_water;
  uint32_t reject_events;
  uint32_t reject_bytes;
} byte_ring_t;

byte_ring_status_t byte_ring_init(byte_ring_t *ring,
                                  uint8_t *storage,
                                  size_t capacity);
byte_ring_status_t byte_ring_write(byte_ring_t *ring,
                                   const uint8_t *data,
                                   size_t length);
size_t byte_ring_peek(const byte_ring_t *ring, uint8_t *data, size_t length);
size_t byte_ring_discard(byte_ring_t *ring, size_t length);
size_t byte_ring_read(byte_ring_t *ring, uint8_t *data, size_t length);
size_t byte_ring_count(const byte_ring_t *ring);
size_t byte_ring_free(const byte_ring_t *ring);

#endif
