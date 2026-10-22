#include "byte_ring.h"

#include <limits.h>

static uint32_t saturating_add_u32(uint32_t value, size_t increment)
{
  if (increment > (size_t)(UINT32_MAX - value))
  {
    return UINT32_MAX;
  }
  return value + (uint32_t)increment;
}

byte_ring_status_t byte_ring_init(byte_ring_t *ring,
                                  uint8_t *storage,
                                  size_t capacity)
{
  if ((ring == NULL) || (storage == NULL) || (capacity == 0U))
  {
    return BYTE_RING_INVALID_ARGUMENT;
  }

  ring->storage = storage;
  ring->capacity = capacity;
  ring->head = 0U;
  ring->tail = 0U;
  ring->count = 0U;
  ring->high_water = 0U;
  ring->reject_events = 0U;
  ring->reject_bytes = 0U;
  return BYTE_RING_OK;
}

byte_ring_status_t byte_ring_write(byte_ring_t *ring,
                                   const uint8_t *data,
                                   size_t length)
{
  size_t first;
  size_t second;
  size_t index;

  if ((ring == NULL) || ((data == NULL) && (length != 0U)) ||
      (ring->storage == NULL) || (ring->capacity == 0U))
  {
    return BYTE_RING_INVALID_ARGUMENT;
  }
  if (length > (ring->capacity - ring->count))
  {
    ring->reject_events = saturating_add_u32(ring->reject_events, 1U);
    ring->reject_bytes = saturating_add_u32(ring->reject_bytes, length);
    return BYTE_RING_NO_SPACE;
  }

  first = ring->capacity - ring->head;
  if (first > length)
  {
    first = length;
  }
  for (index = 0U; index < first; ++index)
  {
    ring->storage[ring->head + index] = data[index];
  }
  second = length - first;
  for (index = 0U; index < second; ++index)
  {
    ring->storage[index] = data[first + index];
  }

  ring->head = (ring->head + length) % ring->capacity;
  ring->count += length;
  if (ring->count > ring->high_water)
  {
    ring->high_water = ring->count;
  }
  return BYTE_RING_OK;
}

size_t byte_ring_peek(const byte_ring_t *ring, uint8_t *data, size_t length)
{
  size_t available;
  size_t first;
  size_t second;
  size_t index;

  if ((ring == NULL) || (data == NULL) || (ring->storage == NULL) ||
      (ring->capacity == 0U))
  {
    return 0U;
  }

  available = ring->count;
  if (length > available)
  {
    length = available;
  }
  first = ring->capacity - ring->tail;
  if (first > length)
  {
    first = length;
  }
  for (index = 0U; index < first; ++index)
  {
    data[index] = ring->storage[ring->tail + index];
  }
  second = length - first;
  for (index = 0U; index < second; ++index)
  {
    data[first + index] = ring->storage[index];
  }
  return length;
}

size_t byte_ring_discard(byte_ring_t *ring, size_t length)
{
  if ((ring == NULL) || (ring->storage == NULL) || (ring->capacity == 0U))
  {
    return 0U;
  }
  if (length > ring->count)
  {
    length = ring->count;
  }
  ring->tail = (ring->tail + length) % ring->capacity;
  ring->count -= length;
  return length;
}

size_t byte_ring_read(byte_ring_t *ring, uint8_t *data, size_t length)
{
  size_t copied;

  copied = byte_ring_peek(ring, data, length);
  (void)byte_ring_discard(ring, copied);
  return copied;
}

size_t byte_ring_count(const byte_ring_t *ring)
{
  return (ring == NULL) ? 0U : ring->count;
}

size_t byte_ring_free(const byte_ring_t *ring)
{
  if ((ring == NULL) || (ring->capacity < ring->count))
  {
    return 0U;
  }
  return ring->capacity - ring->count;
}
