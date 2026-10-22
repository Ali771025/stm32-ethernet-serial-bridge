#include "u8b_basic_path.h"

#include <limits.h>
#include <string.h>

static uint32_t add_u32(uint32_t value, uint32_t increment)
{
  return (increment > (UINT32_MAX - value)) ? UINT32_MAX : value + increment;
}

u8b_basic_status_t u8b_basic_path_init(u8b_basic_path_t *path,
                                       uint32_t local_session_id)
{
  if ((path == NULL) || (local_session_id == 0U))
  {
    return U8B_BASIC_INVALID_ARGUMENT;
  }
  (void)memset(path, 0, sizeof(*path));
  path->local_session_id = local_session_id;
  path->tx_sequence = 1U;
  path->rx_expected_sequence = 1U;
  return U8B_BASIC_OK;
}

u8b_basic_status_t u8b_basic_encode_hello(const u8b_basic_path_t *path,
                                          uint8_t *output,
                                          size_t output_capacity,
                                          size_t *output_length)
{
  u8b_header_t header;

  if ((path == NULL) || (output == NULL) || (output_length == NULL))
  {
    return U8B_BASIC_INVALID_ARGUMENT;
  }
  (void)memset(&header, 0, sizeof(header));
  header.type = U8B_TYPE_HELLO;
  header.session_id = path->local_session_id;
  return (u8b_encode_control(&header,
                             NULL,
                             0U,
                             output,
                             output_capacity,
                             output_length) == U8B_STATUS_OK) ?
         U8B_BASIC_OK : U8B_BASIC_CODEC_ERROR;
}

u8b_basic_status_t u8b_basic_accept_hello(u8b_basic_path_t *path,
                                          const uint8_t *datagram,
                                          size_t datagram_length,
                                          uint8_t *peer_changed)
{
  u8b_datagram_view_t view;
  u8b_status_t codec_status;

  if ((path == NULL) || (datagram == NULL) || (peer_changed == NULL))
  {
    return U8B_BASIC_INVALID_ARGUMENT;
  }
  *peer_changed = 0U;
  codec_status = u8b_decode_datagram(datagram, datagram_length, &view);
  if (codec_status != U8B_STATUS_OK)
  {
    path->counters.codec_errors = add_u32(path->counters.codec_errors, 1U);
    if (codec_status == U8B_STATUS_BAD_CRC)
    {
      path->counters.crc_errors = add_u32(path->counters.crc_errors, 1U);
    }
    return U8B_BASIC_CODEC_ERROR;
  }
  if ((view.header.type != U8B_TYPE_HELLO) ||
      (view.payload_len != 0U) ||
      (view.header.session_id == 0U))
  {
    return U8B_BASIC_NOT_HELLO;
  }

  path->counters.hello_rx = add_u32(path->counters.hello_rx, 1U);
  if ((path->peer_session_valid == 0U) ||
      (path->peer_session_id != view.header.session_id))
  {
    path->peer_session_id = view.header.session_id;
    path->peer_session_valid = 1U;
    path->rx_expected_sequence = 1U;
    (void)memset(path->rx_stream_offsets, 0,
                 sizeof(path->rx_stream_offsets));
    path->counters.peer_session_changes =
      add_u32(path->counters.peer_session_changes, 1U);
    *peer_changed = 1U;
  }
  return U8B_BASIC_OK;
}

u8b_basic_status_t u8b_basic_prepare_tx(
  const u8b_basic_path_t *path,
  const u8b_basic_channel_input_t inputs[U8B_BASIC_CHANNEL_COUNT],
  uint8_t *output,
  size_t output_capacity,
  size_t *output_length,
  u8b_basic_tx_plan_t *plan)
{
  u8b_header_t header;
  u8b_record_t records[U8B_MAX_RECORDS];
  size_t remaining = U8B_MAX_PAYLOAD;
  uint8_t record_count = 0U;
  uint8_t step;
  uint8_t last_channel = path != NULL ? path->tx_cursor : 0U;

  if ((path == NULL) || (inputs == NULL) || (output == NULL) ||
      (output_length == NULL) || (plan == NULL))
  {
    return U8B_BASIC_INVALID_ARGUMENT;
  }
  (void)memset(plan, 0, sizeof(*plan));
  (void)memset(records, 0, sizeof(records));

  for (step = 0U; step < U8B_BASIC_CHANNEL_COUNT; ++step)
  {
    const uint8_t channel =
      (uint8_t)((path->tx_cursor + step) % U8B_BASIC_CHANNEL_COUNT);
    size_t length = inputs[channel].length;

    if (length == 0U)
    {
      continue;
    }
    if (inputs[channel].data == NULL)
    {
      return U8B_BASIC_INVALID_ARGUMENT;
    }
    if (remaining <= U8B_RECORD_HEADER_SIZE)
    {
      break;
    }
    if (length > U8B_BASIC_RECORD_DATA_MAX)
    {
      length = U8B_BASIC_RECORD_DATA_MAX;
    }
    if (length > remaining - U8B_RECORD_HEADER_SIZE)
    {
      length = remaining - U8B_RECORD_HEADER_SIZE;
    }

    records[record_count].channel_id = (uint8_t)(channel + 1U);
    records[record_count].data_len = (uint16_t)length;
    records[record_count].stream_offset = path->tx_stream_offsets[channel];
    records[record_count].data = inputs[channel].data;
    plan->channel_lengths[channel] = (uint16_t)length;
    remaining -= U8B_RECORD_HEADER_SIZE + length;
    last_channel = channel;
    ++record_count;
  }

  if (record_count == 0U)
  {
    return U8B_BASIC_NO_DATA;
  }
  (void)memset(&header, 0, sizeof(header));
  header.type = U8B_TYPE_DATA;
  header.session_id = path->local_session_id;
  header.datagram_seq = path->tx_sequence;
  if (u8b_encode_data(&header,
                      records,
                      record_count,
                      output,
                      output_capacity,
                      output_length) != U8B_STATUS_OK)
  {
    return U8B_BASIC_CODEC_ERROR;
  }
  plan->datagram_sequence = path->tx_sequence;
  plan->record_count = record_count;
  plan->next_cursor =
    (uint8_t)((last_channel + 1U) % U8B_BASIC_CHANNEL_COUNT);
  return U8B_BASIC_OK;
}

u8b_basic_status_t u8b_basic_commit_tx(u8b_basic_path_t *path,
                                       const u8b_basic_tx_plan_t *plan)
{
  uint8_t channel;
  uint32_t bytes = 0U;

  if ((path == NULL) || (plan == NULL))
  {
    return U8B_BASIC_INVALID_ARGUMENT;
  }
  if ((plan->record_count == 0U) ||
      (plan->record_count > U8B_MAX_RECORDS) ||
      (plan->datagram_sequence != path->tx_sequence))
  {
    return U8B_BASIC_STALE_PLAN;
  }
  for (channel = 0U; channel < U8B_BASIC_CHANNEL_COUNT; ++channel)
  {
    path->tx_stream_offsets[channel] += plan->channel_lengths[channel];
    bytes = add_u32(bytes, plan->channel_lengths[channel]);
  }
  ++path->tx_sequence;
  path->tx_cursor = plan->next_cursor;
  path->counters.tx_datagrams = add_u32(path->counters.tx_datagrams, 1U);
  path->counters.tx_bytes = add_u32(path->counters.tx_bytes, bytes);
  return U8B_BASIC_OK;
}

u8b_basic_status_t u8b_basic_prepare_rx(u8b_basic_path_t *path,
                                        const uint8_t *datagram,
                                        size_t datagram_length,
                                        u8b_basic_rx_plan_t *plan)
{
  u8b_datagram_view_t view;
  u8b_status_t codec_status;
  uint8_t index;

  if ((path == NULL) || (datagram == NULL) || (plan == NULL))
  {
    return U8B_BASIC_INVALID_ARGUMENT;
  }
  (void)memset(plan, 0, sizeof(*plan));
  codec_status = u8b_decode_datagram(datagram, datagram_length, &view);
  if (codec_status != U8B_STATUS_OK)
  {
    path->counters.codec_errors = add_u32(path->counters.codec_errors, 1U);
    if (codec_status == U8B_STATUS_BAD_CRC)
    {
      path->counters.crc_errors = add_u32(path->counters.crc_errors, 1U);
    }
    return U8B_BASIC_CODEC_ERROR;
  }
  if (view.header.type != U8B_TYPE_DATA)
  {
    return U8B_BASIC_CODEC_ERROR;
  }
  if (path->peer_session_valid == 0U)
  {
    path->counters.unknown_session = add_u32(path->counters.unknown_session, 1U);
    return U8B_BASIC_PEER_UNKNOWN;
  }
  if (view.header.session_id != path->peer_session_id)
  {
    path->counters.session_mismatch =
      add_u32(path->counters.session_mismatch, 1U);
    return U8B_BASIC_SESSION_MISMATCH;
  }
  if (view.header.datagram_seq != path->rx_expected_sequence)
  {
    if (u8b_sequence_after(path->rx_expected_sequence,
                           view.header.datagram_seq) != 0)
    {
      path->counters.duplicates = add_u32(path->counters.duplicates, 1U);
      return U8B_BASIC_DUPLICATE;
    }
    path->counters.sequence_gaps = add_u32(path->counters.sequence_gaps, 1U);
    return U8B_BASIC_SEQUENCE_GAP;
  }

  plan->datagram_sequence = view.header.datagram_seq;
  plan->record_count = view.record_count;
  (void)memcpy(plan->next_stream_offsets,
               path->rx_stream_offsets,
               sizeof(plan->next_stream_offsets));
  for (index = 0U; index < view.record_count; ++index)
  {
    u8b_record_t record;
    uint8_t channel;

    if (u8b_datagram_record(&view, index, &record) != U8B_STATUS_OK)
    {
      path->counters.codec_errors = add_u32(path->counters.codec_errors, 1U);
      return U8B_BASIC_CODEC_ERROR;
    }
    channel = (uint8_t)(record.channel_id - 1U);
    if (record.stream_offset != plan->next_stream_offsets[channel])
    {
      if (u8b_sequence_after(plan->next_stream_offsets[channel],
                             record.stream_offset) != 0)
      {
        path->counters.stream_duplicates =
          add_u32(path->counters.stream_duplicates, 1U);
        return U8B_BASIC_STREAM_DUPLICATE;
      }
      path->counters.stream_gaps = add_u32(path->counters.stream_gaps, 1U);
      return U8B_BASIC_STREAM_GAP;
    }
    plan->records[index] = record;
    plan->next_stream_offsets[channel] += record.data_len;
  }
  return U8B_BASIC_OK;
}

u8b_basic_status_t u8b_basic_commit_rx(u8b_basic_path_t *path,
                                       const u8b_basic_rx_plan_t *plan)
{
  uint8_t index;
  uint32_t bytes = 0U;

  if ((path == NULL) || (plan == NULL))
  {
    return U8B_BASIC_INVALID_ARGUMENT;
  }
  if ((plan->record_count == 0U) ||
      (plan->record_count > U8B_MAX_RECORDS) ||
      (plan->datagram_sequence != path->rx_expected_sequence))
  {
    return U8B_BASIC_STALE_PLAN;
  }
  for (index = 0U; index < plan->record_count; ++index)
  {
    bytes = add_u32(bytes, plan->records[index].data_len);
  }
  (void)memcpy(path->rx_stream_offsets,
               plan->next_stream_offsets,
               sizeof(path->rx_stream_offsets));
  ++path->rx_expected_sequence;
  path->counters.rx_datagrams = add_u32(path->counters.rx_datagrams, 1U);
  path->counters.rx_bytes = add_u32(path->counters.rx_bytes, bytes);
  return U8B_BASIC_OK;
}
