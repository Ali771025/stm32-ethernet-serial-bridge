#include "u8b_reliable.h"

#include <limits.h>
#include <string.h>

static uint32_t add_u32(uint32_t value, uint32_t increment)
{
  return (increment > (UINT32_MAX - value)) ? UINT32_MAX : value + increment;
}

static uint32_t retry_timeout(uint8_t retry_count)
{
  uint32_t timeout = U8B_RELIABLE_INITIAL_RTO_MS;
  uint8_t index;

  for (index = 0U; index < retry_count; ++index)
  {
    if (timeout >= (U8B_RELIABLE_MAX_RTO_MS / 2U))
    {
      return U8B_RELIABLE_MAX_RTO_MS;
    }
    timeout *= 2U;
  }
  return timeout;
}

static uint8_t elapsed(uint32_t now, uint32_t then, uint32_t interval)
{
  return ((uint32_t)(now - then) >= interval) ? 1U : 0U;
}

static void write_be32(uint8_t *output, uint32_t value)
{
  output[0] = (uint8_t)(value >> 24);
  output[1] = (uint8_t)(value >> 16);
  output[2] = (uint8_t)(value >> 8);
  output[3] = (uint8_t)value;
}

static uint32_t read_be32(const uint8_t *input)
{
  return ((uint32_t)input[0] << 24) |
         ((uint32_t)input[1] << 16) |
         ((uint32_t)input[2] << 8) |
         (uint32_t)input[3];
}

static uint32_t first_tx_sequence(const u8b_reliable_t *state)
{
  uint32_t first = state->tx_next_sequence;
  uint8_t found = 0U;
  uint8_t index;

  for (index = 0U; index < U8B_RELIABLE_WINDOW_SIZE; ++index)
  {
    if ((state->tx_slots[index].occupied != 0U) &&
        ((found == 0U) ||
         (u8b_sequence_after(first,
                             state->tx_slots[index].sequence) != 0)))
    {
      first = state->tx_slots[index].sequence;
      found = 1U;
    }
  }
  return first;
}

static u8b_reliable_tx_slot_t *find_tx_slot(u8b_reliable_t *state,
                                             uint32_t sequence)
{
  uint8_t index;
  for (index = 0U; index < U8B_RELIABLE_WINDOW_SIZE; ++index)
  {
    if ((state->tx_slots[index].occupied != 0U) &&
        (state->tx_slots[index].sequence == sequence))
    {
      return &state->tx_slots[index];
    }
  }
  return NULL;
}

static u8b_reliable_rx_slot_t *find_rx_slot(u8b_reliable_t *state,
                                             uint32_t sequence)
{
  uint8_t index;
  for (index = 0U; index < U8B_RELIABLE_WINDOW_SIZE; ++index)
  {
    if ((state->rx_slots[index].occupied != 0U) &&
        (state->rx_slots[index].sequence == sequence))
    {
      return &state->rx_slots[index];
    }
  }
  return NULL;
}

static uint32_t ack_bitmap(const u8b_reliable_t *state)
{
  uint32_t bitmap = 0U;
  uint8_t index;

  for (index = 0U; index < U8B_RELIABLE_WINDOW_SIZE; ++index)
  {
    if (state->rx_slots[index].occupied != 0U)
    {
      uint32_t distance = state->rx_slots[index].sequence - state->rx_ack_base;
      if ((distance >= 1U) && (distance <= 32U))
      {
        bitmap |= UINT32_C(1) << (distance - 1U);
      }
    }
  }
  return bitmap;
}

static uint8_t sequence_acked(uint32_t sequence,
                              uint32_t base,
                              uint32_t bitmap)
{
  uint32_t distance;

  if ((sequence == base) || (u8b_sequence_after(base, sequence) != 0))
  {
    return 1U;
  }
  distance = sequence - base;
  if ((distance >= 1U) && (distance <= 32U) &&
      ((bitmap & (UINT32_C(1) << (distance - 1U))) != 0U))
  {
    return 1U;
  }
  return 0U;
}

static u8b_reliable_status_t decode_for_state(u8b_reliable_t *state,
                                               const uint8_t *datagram,
                                               size_t length,
                                               u8b_datagram_view_t *view)
{
  u8b_status_t status = u8b_decode_datagram(datagram, length, view);
  if (status != U8B_STATUS_OK)
  {
    state->counters.codec_errors = add_u32(state->counters.codec_errors, 1U);
    if (status == U8B_STATUS_BAD_CRC)
    {
      state->counters.crc_errors = add_u32(state->counters.crc_errors, 1U);
    }
    return U8B_RELIABLE_CODEC_ERROR;
  }
  return U8B_RELIABLE_OK;
}

u8b_reliable_status_t u8b_reliable_init(u8b_reliable_t *state,
                                        uint32_t local_session_id)
{
  if ((state == NULL) || (local_session_id == 0U))
  {
    return U8B_RELIABLE_INVALID_ARGUMENT;
  }
  (void)memset(state, 0, sizeof(*state));
  state->local_session_id = local_session_id;
  state->tx_next_sequence = 1U;
  state->rx_deliver_next = 1U;
  return U8B_RELIABLE_OK;
}

u8b_reliable_status_t u8b_reliable_encode_hello(const u8b_reliable_t *state,
                                                uint8_t *output,
                                                size_t capacity,
                                                size_t *length)
{
  u8b_header_t header;
  uint8_t payload[4];
  if ((state == NULL) || (output == NULL) || (length == NULL))
  {
    return U8B_RELIABLE_INVALID_ARGUMENT;
  }
  (void)memset(&header, 0, sizeof(header));
  header.type = U8B_TYPE_HELLO;
  header.session_id = state->local_session_id;
  write_be32(payload, first_tx_sequence(state));
  return (u8b_encode_control(&header, payload, sizeof(payload),
                             output, capacity, length) ==
          U8B_STATUS_OK) ? U8B_RELIABLE_OK : U8B_RELIABLE_CODEC_ERROR;
}

u8b_reliable_status_t u8b_reliable_accept_hello(u8b_reliable_t *state,
                                                const uint8_t *datagram,
                                                size_t length,
                                                uint8_t *peer_changed)
{
  u8b_datagram_view_t view;
  uint8_t index;

  if ((state == NULL) || (datagram == NULL) || (peer_changed == NULL))
  {
    return U8B_RELIABLE_INVALID_ARGUMENT;
  }
  *peer_changed = 0U;
  if (decode_for_state(state, datagram, length, &view) != U8B_RELIABLE_OK)
  {
    return U8B_RELIABLE_CODEC_ERROR;
  }
  if ((view.header.type != U8B_TYPE_HELLO) || (view.payload_len != 4U) ||
      (view.header.session_id == 0U))
  {
    return U8B_RELIABLE_CODEC_ERROR;
  }
  if ((state->peer_session_valid == 0U) ||
      (state->peer_session_id != view.header.session_id))
  {
    state->peer_session_id = view.header.session_id;
    state->peer_session_valid = 1U;
    state->rx_deliver_next = read_be32(view.payload);
    state->rx_ack_base = state->rx_deliver_next - 1U;
    state->rx_count = 0U;
    state->ack_pending = 1U;
    (void)memset(state->rx_slots, 0, sizeof(state->rx_slots));
    (void)memset(state->rx_accept_offsets, 0,
                 sizeof(state->rx_accept_offsets));
    (void)memset(state->rx_offset_known, 0,
                 sizeof(state->rx_offset_known));
    for (index = 0U; index < U8B_RELIABLE_WINDOW_SIZE; ++index)
    {
      if (state->tx_slots[index].occupied != 0U)
      {
        state->tx_slots[index].sent = 0U;
        state->tx_slots[index].retry_count = 0U;
        state->tx_slots[index].exhausted = 0U;
      }
    }
    state->counters.peer_session_changes =
      add_u32(state->counters.peer_session_changes, 1U);
    *peer_changed = 1U;
  }
  return U8B_RELIABLE_OK;
}

u8b_reliable_status_t u8b_reliable_queue_data(
  u8b_reliable_t *state,
  const u8b_reliable_channel_input_t inputs[U8B_RELIABLE_CHANNEL_COUNT],
  u8b_reliable_queue_result_t *result)
{
  u8b_reliable_tx_slot_t *slot = NULL;
  u8b_header_t header;
  u8b_record_t records[U8B_MAX_RECORDS];
  size_t remaining = U8B_MAX_PAYLOAD;
  size_t encoded_length = 0U;
  uint8_t index;
  uint8_t step;
  uint8_t count = 0U;
  uint8_t last_channel;

  if ((state == NULL) || (inputs == NULL) || (result == NULL))
  {
    return U8B_RELIABLE_INVALID_ARGUMENT;
  }
  if (state->tx_count >= U8B_RELIABLE_WINDOW_SIZE)
  {
    return U8B_RELIABLE_WINDOW_FULL;
  }
  for (index = 0U; index < U8B_RELIABLE_WINDOW_SIZE; ++index)
  {
    if (state->tx_slots[index].occupied == 0U)
    {
      slot = &state->tx_slots[index];
      break;
    }
  }
  if (slot == NULL)
  {
    return U8B_RELIABLE_WINDOW_FULL;
  }

  (void)memset(result, 0, sizeof(*result));
  (void)memset(records, 0, sizeof(records));
  last_channel = state->tx_cursor;
  for (step = 0U; step < U8B_RELIABLE_CHANNEL_COUNT; ++step)
  {
    uint8_t channel = (uint8_t)((state->tx_cursor + step) %
                                U8B_RELIABLE_CHANNEL_COUNT);
    size_t data_length = inputs[channel].length;
    if (data_length == 0U)
    {
      continue;
    }
    if (inputs[channel].data == NULL)
    {
      return U8B_RELIABLE_INVALID_ARGUMENT;
    }
    if (remaining <= U8B_RECORD_HEADER_SIZE)
    {
      break;
    }
    if (data_length > U8B_RELIABLE_RECORD_DATA_MAX)
    {
      data_length = U8B_RELIABLE_RECORD_DATA_MAX;
    }
    if (data_length > (remaining - U8B_RECORD_HEADER_SIZE))
    {
      data_length = remaining - U8B_RECORD_HEADER_SIZE;
    }
    records[count].channel_id = (uint8_t)(channel + 1U);
    records[count].data_len = (uint16_t)data_length;
    records[count].stream_offset = state->tx_stream_offsets[channel];
    records[count].data = inputs[channel].data;
    result->channel_lengths[channel] = (uint16_t)data_length;
    remaining -= U8B_RECORD_HEADER_SIZE + data_length;
    last_channel = channel;
    ++count;
  }
  if (count == 0U)
  {
    return U8B_RELIABLE_NO_DATA;
  }
  (void)memset(&header, 0, sizeof(header));
  header.type = U8B_TYPE_DATA;
  header.session_id = state->local_session_id;
  header.datagram_seq = state->tx_next_sequence;
  if (u8b_encode_data(&header, records, count, slot->data,
                      sizeof(slot->data), &encoded_length) != U8B_STATUS_OK)
  {
    return U8B_RELIABLE_CODEC_ERROR;
  }
  slot->sequence = state->tx_next_sequence;
  slot->length = (uint16_t)encoded_length;
  slot->occupied = 1U;
  slot->sent = 0U;
  slot->retry_count = 0U;
  slot->exhausted = 0U;
  result->sequence = state->tx_next_sequence;
  result->record_count = count;
  for (index = 0U; index < U8B_RELIABLE_CHANNEL_COUNT; ++index)
  {
    state->tx_stream_offsets[index] += result->channel_lengths[index];
  }
  ++state->tx_next_sequence;
  state->tx_cursor = (uint8_t)((last_channel + 1U) %
                               U8B_RELIABLE_CHANNEL_COUNT);
  ++state->tx_count;
  state->counters.queued_datagrams =
    add_u32(state->counters.queued_datagrams, 1U);
  state->counters.queued_bytes =
    add_u32(state->counters.queued_bytes, (uint32_t)encoded_length);
  if (state->tx_count > state->counters.tx_window_high_water)
  {
    state->counters.tx_window_high_water = state->tx_count;
  }
  return U8B_RELIABLE_OK;
}

u8b_reliable_status_t u8b_reliable_next_tx(u8b_reliable_t *state,
                                           uint32_t now_ms,
                                           u8b_reliable_tx_view_t *view)
{
  u8b_reliable_tx_slot_t *selected = NULL;
  uint8_t index;

  if ((state == NULL) || (view == NULL))
  {
    return U8B_RELIABLE_INVALID_ARGUMENT;
  }
  for (index = 0U; index < U8B_RELIABLE_WINDOW_SIZE; ++index)
  {
    u8b_reliable_tx_slot_t *slot = &state->tx_slots[index];
    uint8_t due = 0U;
    if (slot->occupied == 0U)
    {
      continue;
    }
    if (slot->sent == 0U)
    {
      due = 1U;
    }
    else if ((slot->exhausted == 0U) &&
             (elapsed(now_ms, slot->last_send_ms,
                      retry_timeout(slot->retry_count)) != 0U))
    {
      if (slot->retry_count >= U8B_RELIABLE_MAX_RETRIES)
      {
        slot->exhausted = 1U;
        state->counters.delivery_failures =
          add_u32(state->counters.delivery_failures, 1U);
        state->counters.degraded = 1U;
      }
      else
      {
        due = 1U;
      }
    }
    if ((due != 0U) &&
        ((selected == NULL) ||
         (u8b_sequence_after(selected->sequence, slot->sequence) != 0)))
    {
      selected = slot;
    }
  }
  if (selected == NULL)
  {
    return U8B_RELIABLE_NO_WORK;
  }
  view->data = selected->data;
  view->length = selected->length;
  view->sequence = selected->sequence;
  view->is_retry = selected->sent;
  return U8B_RELIABLE_OK;
}

u8b_reliable_status_t u8b_reliable_commit_tx(u8b_reliable_t *state,
                                             uint32_t sequence,
                                             uint32_t now_ms)
{
  u8b_reliable_tx_slot_t *slot;
  if (state == NULL)
  {
    return U8B_RELIABLE_INVALID_ARGUMENT;
  }
  slot = find_tx_slot(state, sequence);
  if (slot == NULL)
  {
    return U8B_RELIABLE_STALE_TOKEN;
  }
  if (slot->sent != 0U)
  {
    ++slot->retry_count;
    state->counters.retransmissions =
      add_u32(state->counters.retransmissions, 1U);
  }
  else
  {
    slot->sent = 1U;
    state->counters.initial_transmissions =
      add_u32(state->counters.initial_transmissions, 1U);
  }
  slot->last_send_ms = now_ms;
  return U8B_RELIABLE_OK;
}

static void advance_ack_base(u8b_reliable_t *state)
{
  for (;;)
  {
    uint32_t next = state->rx_ack_base + 1U;
    u8b_reliable_rx_slot_t *slot = find_rx_slot(state, next);
    u8b_datagram_view_t view;
    uint32_t offsets[U8B_RELIABLE_CHANNEL_COUNT];
    uint8_t offset_known[U8B_RELIABLE_CHANNEL_COUNT];
    uint8_t record_index;
    uint8_t valid = 1U;

    if ((slot == NULL) || (slot->accepted != 0U))
    {
      return;
    }
    if (u8b_decode_datagram(slot->data, slot->length, &view) != U8B_STATUS_OK)
    {
      return;
    }
    (void)memcpy(offsets, state->rx_accept_offsets, sizeof(offsets));
    (void)memcpy(offset_known, state->rx_offset_known, sizeof(offset_known));
    for (record_index = 0U; record_index < view.record_count; ++record_index)
    {
      u8b_record_t record;
      uint8_t channel;
      if (u8b_datagram_record(&view, record_index, &record) != U8B_STATUS_OK)
      {
        valid = 0U;
        break;
      }
      channel = (uint8_t)(record.channel_id - 1U);
      if (offset_known[channel] == 0U)
      {
        offsets[channel] = record.stream_offset;
        offset_known[channel] = 1U;
      }
      else if (record.stream_offset != offsets[channel])
      {
        valid = 0U;
        break;
      }
      offsets[channel] += record.data_len;
    }
    if (valid == 0U)
    {
      slot->occupied = 0U;
      --state->rx_count;
      state->counters.stream_errors =
        add_u32(state->counters.stream_errors, 1U);
      state->counters.degraded = 1U;
      return;
    }
    (void)memcpy(state->rx_accept_offsets, offsets, sizeof(offsets));
    (void)memcpy(state->rx_offset_known, offset_known, sizeof(offset_known));
    slot->accepted = 1U;
    state->rx_ack_base = next;
  }
}

u8b_reliable_status_t u8b_reliable_accept_data(u8b_reliable_t *state,
                                               const uint8_t *datagram,
                                               size_t length)
{
  u8b_datagram_view_t view;
  u8b_reliable_rx_slot_t *slot = NULL;
  uint32_t distance;
  uint8_t index;

  if ((state == NULL) || (datagram == NULL) ||
      (length > U8B_MAX_DATAGRAM))
  {
    return U8B_RELIABLE_INVALID_ARGUMENT;
  }
  if (decode_for_state(state, datagram, length, &view) != U8B_RELIABLE_OK)
  {
    return U8B_RELIABLE_CODEC_ERROR;
  }
  if (view.header.type != U8B_TYPE_DATA)
  {
    return U8B_RELIABLE_CODEC_ERROR;
  }
  if (state->peer_session_valid == 0U)
  {
    return U8B_RELIABLE_PEER_UNKNOWN;
  }
  if (view.header.session_id != state->peer_session_id)
  {
    state->counters.session_mismatches =
      add_u32(state->counters.session_mismatches, 1U);
    return U8B_RELIABLE_SESSION_MISMATCH;
  }
  if ((view.header.datagram_seq == state->rx_ack_base) ||
      (u8b_sequence_after(state->rx_ack_base,
                          view.header.datagram_seq) != 0) ||
      (find_rx_slot(state, view.header.datagram_seq) != NULL))
  {
    state->counters.duplicates = add_u32(state->counters.duplicates, 1U);
    state->ack_pending = 1U;
    return U8B_RELIABLE_DUPLICATE;
  }
  distance = view.header.datagram_seq - state->rx_ack_base;
  if ((distance == 0U) || (distance > U8B_RELIABLE_WINDOW_SIZE) ||
      (state->rx_count >= U8B_RELIABLE_WINDOW_SIZE))
  {
    state->counters.outside_window =
      add_u32(state->counters.outside_window, 1U);
    state->ack_pending = 1U;
    return U8B_RELIABLE_OUTSIDE_WINDOW;
  }
  for (index = 0U; index < U8B_RELIABLE_WINDOW_SIZE; ++index)
  {
    if (state->rx_slots[index].occupied == 0U)
    {
      slot = &state->rx_slots[index];
      break;
    }
  }
  if (slot == NULL)
  {
    return U8B_RELIABLE_WINDOW_FULL;
  }
  (void)memcpy(slot->data, datagram, length);
  slot->sequence = view.header.datagram_seq;
  slot->length = (uint16_t)length;
  slot->occupied = 1U;
  slot->accepted = 0U;
  ++state->rx_count;
  state->counters.data_rx = add_u32(state->counters.data_rx, 1U);
  state->counters.data_rx_bytes =
    add_u32(state->counters.data_rx_bytes, (uint32_t)length);
  if (distance > 1U)
  {
    state->counters.reordered = add_u32(state->counters.reordered, 1U);
  }
  if (state->rx_count > state->counters.rx_window_high_water)
  {
    state->counters.rx_window_high_water = state->rx_count;
  }
  advance_ack_base(state);
  state->ack_pending = 1U;
  return U8B_RELIABLE_OK;
}

u8b_reliable_status_t u8b_reliable_encode_ack(u8b_reliable_t *state,
                                              uint8_t *output,
                                              size_t capacity,
                                              size_t *length)
{
  u8b_header_t header;
  if ((state == NULL) || (output == NULL) || (length == NULL))
  {
    return U8B_RELIABLE_INVALID_ARGUMENT;
  }
  (void)memset(&header, 0, sizeof(header));
  header.type = U8B_TYPE_ACK;
  header.session_id = state->local_session_id;
  header.ack_base = state->rx_ack_base;
  header.ack_bitmap = ack_bitmap(state);
  if (u8b_encode_control(&header, NULL, 0U, output, capacity, length) !=
      U8B_STATUS_OK)
  {
    return U8B_RELIABLE_CODEC_ERROR;
  }
  return U8B_RELIABLE_OK;
}

u8b_reliable_status_t u8b_reliable_commit_ack(u8b_reliable_t *state)
{
  if (state == NULL)
  {
    return U8B_RELIABLE_INVALID_ARGUMENT;
  }
  state->ack_pending = 0U;
  state->counters.acknowledgements_tx =
    add_u32(state->counters.acknowledgements_tx, 1U);
  return U8B_RELIABLE_OK;
}

u8b_reliable_status_t u8b_reliable_accept_ack(u8b_reliable_t *state,
                                              const uint8_t *datagram,
                                              size_t length)
{
  u8b_datagram_view_t view;
  uint8_t index;
  if ((state == NULL) || (datagram == NULL))
  {
    return U8B_RELIABLE_INVALID_ARGUMENT;
  }
  if (decode_for_state(state, datagram, length, &view) != U8B_RELIABLE_OK)
  {
    return U8B_RELIABLE_CODEC_ERROR;
  }
  if ((view.header.type != U8B_TYPE_ACK) || (view.payload_len != 0U))
  {
    return U8B_RELIABLE_CODEC_ERROR;
  }
  if ((state->peer_session_valid == 0U) ||
      (view.header.session_id != state->peer_session_id))
  {
    state->counters.session_mismatches =
      add_u32(state->counters.session_mismatches, 1U);
    return U8B_RELIABLE_SESSION_MISMATCH;
  }
  state->counters.acknowledgements_rx =
    add_u32(state->counters.acknowledgements_rx, 1U);
  for (index = 0U; index < U8B_RELIABLE_WINDOW_SIZE; ++index)
  {
    u8b_reliable_tx_slot_t *slot = &state->tx_slots[index];
    /* A valid-CRC but premature ACK must never retire queued bytes that have
     * not been put on the wire. This also protects the sender if a peer's
     * ACK base is inconsistent with our current transmit state. */
    if ((slot->occupied != 0U) && (slot->sent != 0U) &&
        (sequence_acked(slot->sequence,
                        view.header.ack_base,
                        view.header.ack_bitmap) != 0U))
    {
      slot->occupied = 0U;
      --state->tx_count;
      state->counters.acknowledged_datagrams =
        add_u32(state->counters.acknowledged_datagrams, 1U);
    }
  }
  return U8B_RELIABLE_OK;
}

u8b_reliable_status_t u8b_reliable_peek_delivery(
  u8b_reliable_t *state,
  u8b_reliable_delivery_t *delivery)
{
  u8b_reliable_rx_slot_t *slot;
  u8b_datagram_view_t view;
  uint8_t index;
  if ((state == NULL) || (delivery == NULL))
  {
    return U8B_RELIABLE_INVALID_ARGUMENT;
  }
  slot = find_rx_slot(state, state->rx_deliver_next);
  if ((slot == NULL) || (slot->accepted == 0U))
  {
    return U8B_RELIABLE_NO_WORK;
  }
  if (u8b_decode_datagram(slot->data, slot->length, &view) != U8B_STATUS_OK)
  {
    return U8B_RELIABLE_CODEC_ERROR;
  }
  (void)memset(delivery, 0, sizeof(*delivery));
  delivery->sequence = slot->sequence;
  delivery->record_count = view.record_count;
  delivery->slot_index = (uint8_t)(slot - state->rx_slots);
  for (index = 0U; index < view.record_count; ++index)
  {
    if (u8b_datagram_record(&view, index, &delivery->records[index]) !=
        U8B_STATUS_OK)
    {
      return U8B_RELIABLE_CODEC_ERROR;
    }
  }
  return U8B_RELIABLE_OK;
}

u8b_reliable_status_t u8b_reliable_commit_delivery(
  u8b_reliable_t *state,
  const u8b_reliable_delivery_t *delivery)
{
  u8b_reliable_rx_slot_t *slot;
  uint32_t bytes = 0U;
  uint8_t index;
  if ((state == NULL) || (delivery == NULL) ||
      (delivery->slot_index >= U8B_RELIABLE_WINDOW_SIZE))
  {
    return U8B_RELIABLE_INVALID_ARGUMENT;
  }
  slot = &state->rx_slots[delivery->slot_index];
  if ((slot->occupied == 0U) || (slot->accepted == 0U) ||
      (slot->sequence != state->rx_deliver_next) ||
      (slot->sequence != delivery->sequence))
  {
    return U8B_RELIABLE_STALE_TOKEN;
  }
  for (index = 0U; index < delivery->record_count; ++index)
  {
    bytes = add_u32(bytes, delivery->records[index].data_len);
  }
  slot->occupied = 0U;
  slot->accepted = 0U;
  --state->rx_count;
  ++state->rx_deliver_next;
  state->counters.delivered_datagrams =
    add_u32(state->counters.delivered_datagrams, 1U);
  state->counters.delivered_bytes =
    add_u32(state->counters.delivered_bytes, bytes);
  return U8B_RELIABLE_OK;
}

uint8_t u8b_reliable_has_due_tx(u8b_reliable_t *state, uint32_t now_ms)
{
  u8b_reliable_tx_view_t view;
  return (u8b_reliable_next_tx(state, now_ms, &view) == U8B_RELIABLE_OK) ?
         1U : 0U;
}

uint8_t u8b_reliable_has_delivery(const u8b_reliable_t *state)
{
  uint8_t index;
  if (state == NULL)
  {
    return 0U;
  }
  for (index = 0U; index < U8B_RELIABLE_WINDOW_SIZE; ++index)
  {
    if ((state->rx_slots[index].occupied != 0U) &&
        (state->rx_slots[index].accepted != 0U) &&
        (state->rx_slots[index].sequence == state->rx_deliver_next))
    {
      return 1U;
    }
  }
  return 0U;
}

uint8_t u8b_reliable_ack_pending(const u8b_reliable_t *state)
{
  return ((state != NULL) && (state->ack_pending != 0U)) ? 1U : 0U;
}
