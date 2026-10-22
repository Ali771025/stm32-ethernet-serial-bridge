#include "u8b_protocol.h"

#include <string.h>

#define U8B_CRC_OFFSET 28U

static uint16_t u8b_read_be16(const uint8_t *source)
{
  return (uint16_t)(((uint16_t)source[0] << 8) | (uint16_t)source[1]);
}

static uint32_t u8b_read_be32(const uint8_t *source)
{
  return ((uint32_t)source[0] << 24) |
         ((uint32_t)source[1] << 16) |
         ((uint32_t)source[2] << 8) |
         (uint32_t)source[3];
}

static void u8b_write_be16(uint8_t *destination, uint16_t value)
{
  destination[0] = (uint8_t)(value >> 8);
  destination[1] = (uint8_t)value;
}

static void u8b_write_be32(uint8_t *destination, uint32_t value)
{
  destination[0] = (uint8_t)(value >> 24);
  destination[1] = (uint8_t)(value >> 16);
  destination[2] = (uint8_t)(value >> 8);
  destination[3] = (uint8_t)value;
}

static uint32_t u8b_crc32_update(uint32_t crc,
                                 const uint8_t *data,
                                 size_t length)
{
  size_t index;

  for (index = 0U; index < length; ++index)
  {
    uint_fast8_t bit;
    crc ^= (uint32_t)data[index];
    for (bit = 0U; bit < 8U; ++bit)
    {
      const uint32_t mask = (uint32_t)(0U - (crc & 1U));
      crc = (crc >> 1) ^ (UINT32_C(0xEDB88320) & mask);
    }
  }
  return crc;
}

uint32_t u8b_crc32(const uint8_t *data, size_t length)
{
  uint32_t crc = UINT32_C(0xFFFFFFFF);

  if ((data == NULL) && (length != 0U))
  {
    return 0U;
  }
  crc = u8b_crc32_update(crc, data, length);
  return crc ^ UINT32_C(0xFFFFFFFF);
}

static uint32_t u8b_datagram_crc(const uint8_t *datagram, size_t length)
{
  static const uint8_t zero_crc[4] = {0U, 0U, 0U, 0U};
  uint32_t crc = UINT32_C(0xFFFFFFFF);

  crc = u8b_crc32_update(crc, datagram, U8B_CRC_OFFSET);
  crc = u8b_crc32_update(crc, zero_crc, sizeof(zero_crc));
  if (length > U8B_HEADER_SIZE)
  {
    crc = u8b_crc32_update(crc,
                           &datagram[U8B_HEADER_SIZE],
                           length - U8B_HEADER_SIZE);
  }
  return crc ^ UINT32_C(0xFFFFFFFF);
}

static int u8b_type_is_valid(u8b_type_t type)
{
  return ((type >= U8B_TYPE_DATA) && (type <= U8B_TYPE_DIAG)) ? 1 : 0;
}

static u8b_status_t u8b_validate_header_for_encode(const u8b_header_t *header)
{
  if (header == NULL)
  {
    return U8B_STATUS_INVALID_ARGUMENT;
  }
  if (u8b_type_is_valid(header->type) == 0)
  {
    return U8B_STATUS_BAD_TYPE;
  }
  if ((header->flags & (uint8_t)~U8B_HEADER_FLAG_MASK) != 0U)
  {
    return U8B_STATUS_BAD_HEADER_FLAGS;
  }
  return U8B_STATUS_OK;
}

static void u8b_encode_common_header(uint8_t *output,
                                     const u8b_header_t *header,
                                     uint16_t payload_length,
                                     uint8_t record_count)
{
  (void)memset(output, 0, U8B_HEADER_SIZE);
  u8b_write_be32(&output[0], U8B_MAGIC_U32);
  output[4] = U8B_VERSION;
  output[5] = (uint8_t)header->type;
  output[6] = header->flags;
  output[7] = U8B_HEADER_SIZE;
  u8b_write_be32(&output[8], header->session_id);
  u8b_write_be32(&output[12], header->datagram_seq);
  u8b_write_be32(&output[16], header->ack_base);
  u8b_write_be32(&output[20], header->ack_bitmap);
  u8b_write_be16(&output[24], payload_length);
  output[26] = record_count;
}

u8b_status_t u8b_encode_data(const u8b_header_t *header,
                             const u8b_record_t *records,
                             size_t record_count,
                             uint8_t *output,
                             size_t output_capacity,
                             size_t *output_length)
{
  size_t payload_length = 0U;
  size_t output_offset;
  size_t index;
  u8b_status_t status;

  if ((output == NULL) || (output_length == NULL) ||
      ((records == NULL) && (record_count != 0U)))
  {
    return U8B_STATUS_INVALID_ARGUMENT;
  }
  status = u8b_validate_header_for_encode(header);
  if (status != U8B_STATUS_OK)
  {
    return status;
  }
  if (header->type != U8B_TYPE_DATA)
  {
    return U8B_STATUS_BAD_TYPE;
  }
  if (record_count > U8B_MAX_RECORDS)
  {
    return U8B_STATUS_BAD_RECORD_COUNT;
  }

  for (index = 0U; index < record_count; ++index)
  {
    const u8b_record_t *record = &records[index];
    const size_t data_length = (size_t)record->data_len;

    if ((record->channel_id < 1U) || (record->channel_id > 8U))
    {
      return U8B_STATUS_BAD_CHANNEL;
    }
    if ((record->flags & (uint8_t)~U8B_RECORD_FLAG_MASK) != 0U)
    {
      return U8B_STATUS_BAD_RECORD_FLAGS;
    }
    if ((record->data == NULL) && (data_length != 0U))
    {
      return U8B_STATUS_INVALID_ARGUMENT;
    }
    if ((payload_length > U8B_MAX_PAYLOAD - U8B_RECORD_HEADER_SIZE) ||
        (data_length > U8B_MAX_PAYLOAD - payload_length - U8B_RECORD_HEADER_SIZE))
    {
      return U8B_STATUS_BAD_PAYLOAD_LENGTH;
    }
    payload_length += U8B_RECORD_HEADER_SIZE + data_length;
  }

  if (output_capacity < U8B_HEADER_SIZE + payload_length)
  {
    return U8B_STATUS_OUTPUT_TOO_SMALL;
  }

  u8b_encode_common_header(output,
                           header,
                           (uint16_t)payload_length,
                           (uint8_t)record_count);
  output_offset = U8B_HEADER_SIZE;
  for (index = 0U; index < record_count; ++index)
  {
    const u8b_record_t *record = &records[index];
    const size_t data_length = (size_t)record->data_len;

    output[output_offset] = record->channel_id;
    output[output_offset + 1U] = record->flags;
    u8b_write_be16(&output[output_offset + 2U], record->data_len);
    u8b_write_be32(&output[output_offset + 4U], record->stream_offset);
    if (data_length != 0U)
    {
      (void)memcpy(&output[output_offset + U8B_RECORD_HEADER_SIZE],
                   record->data,
                   data_length);
    }
    output_offset += U8B_RECORD_HEADER_SIZE + data_length;
  }
  u8b_write_be32(&output[U8B_CRC_OFFSET],
                 u8b_datagram_crc(output, U8B_HEADER_SIZE + payload_length));
  *output_length = U8B_HEADER_SIZE + payload_length;
  return U8B_STATUS_OK;
}

u8b_status_t u8b_encode_control(const u8b_header_t *header,
                                const uint8_t *payload,
                                size_t payload_length,
                                uint8_t *output,
                                size_t output_capacity,
                                size_t *output_length)
{
  u8b_status_t status;

  if ((output == NULL) || (output_length == NULL) ||
      ((payload == NULL) && (payload_length != 0U)))
  {
    return U8B_STATUS_INVALID_ARGUMENT;
  }
  status = u8b_validate_header_for_encode(header);
  if (status != U8B_STATUS_OK)
  {
    return status;
  }
  if (header->type == U8B_TYPE_DATA)
  {
    return U8B_STATUS_BAD_TYPE;
  }
  if (payload_length > U8B_MAX_PAYLOAD)
  {
    return U8B_STATUS_BAD_PAYLOAD_LENGTH;
  }
  if (output_capacity < U8B_HEADER_SIZE + payload_length)
  {
    return U8B_STATUS_OUTPUT_TOO_SMALL;
  }

  u8b_encode_common_header(output, header, (uint16_t)payload_length, 0U);
  if (payload_length != 0U)
  {
    (void)memcpy(&output[U8B_HEADER_SIZE], payload, payload_length);
  }
  u8b_write_be32(&output[U8B_CRC_OFFSET],
                 u8b_datagram_crc(output, U8B_HEADER_SIZE + payload_length));
  *output_length = U8B_HEADER_SIZE + payload_length;
  return U8B_STATUS_OK;
}

static u8b_status_t u8b_validate_records(const uint8_t *payload,
                                         size_t payload_length,
                                         uint8_t record_count)
{
  size_t offset = 0U;
  uint_fast8_t index;

  for (index = 0U; index < record_count; ++index)
  {
    uint16_t data_length;

    if (payload_length - offset < U8B_RECORD_HEADER_SIZE)
    {
      return U8B_STATUS_RECORD_TRUNCATED;
    }
    if ((payload[offset] < 1U) || (payload[offset] > 8U))
    {
      return U8B_STATUS_BAD_CHANNEL;
    }
    if ((payload[offset + 1U] & (uint8_t)~U8B_RECORD_FLAG_MASK) != 0U)
    {
      return U8B_STATUS_BAD_RECORD_FLAGS;
    }
    data_length = u8b_read_be16(&payload[offset + 2U]);
    offset += U8B_RECORD_HEADER_SIZE;
    if ((size_t)data_length > payload_length - offset)
    {
      return U8B_STATUS_BAD_RECORD_LENGTH;
    }
    offset += (size_t)data_length;
  }
  if (offset != payload_length)
  {
    return U8B_STATUS_TRAILING_DATA;
  }
  return U8B_STATUS_OK;
}

u8b_status_t u8b_decode_datagram(const uint8_t *datagram,
                                 size_t datagram_length,
                                 u8b_datagram_view_t *view)
{
  u8b_datagram_view_t decoded;
  uint16_t payload_length;
  uint8_t record_count;
  u8b_status_t status;

  if ((datagram == NULL) || (view == NULL))
  {
    return U8B_STATUS_INVALID_ARGUMENT;
  }
  if (datagram_length < U8B_HEADER_SIZE)
  {
    return U8B_STATUS_TRUNCATED;
  }
  if (u8b_read_be32(&datagram[0]) != U8B_MAGIC_U32)
  {
    return U8B_STATUS_BAD_MAGIC;
  }
  if (datagram[4] != U8B_VERSION)
  {
    return U8B_STATUS_BAD_VERSION;
  }
  decoded.header.type = (u8b_type_t)datagram[5];
  if (u8b_type_is_valid(decoded.header.type) == 0)
  {
    return U8B_STATUS_BAD_TYPE;
  }
  decoded.header.flags = datagram[6];
  if ((decoded.header.flags & (uint8_t)~U8B_HEADER_FLAG_MASK) != 0U)
  {
    return U8B_STATUS_BAD_HEADER_FLAGS;
  }
  if (datagram[7] != U8B_HEADER_SIZE)
  {
    return U8B_STATUS_BAD_HEADER_LENGTH;
  }
  payload_length = u8b_read_be16(&datagram[24]);
  if ((payload_length > U8B_MAX_PAYLOAD) ||
      (datagram_length != U8B_HEADER_SIZE + (size_t)payload_length))
  {
    return U8B_STATUS_BAD_PAYLOAD_LENGTH;
  }
  record_count = datagram[26];
  if (record_count > U8B_MAX_RECORDS)
  {
    return U8B_STATUS_BAD_RECORD_COUNT;
  }
  if (datagram[27] != 0U)
  {
    return U8B_STATUS_BAD_RESERVED;
  }
  if (u8b_read_be32(&datagram[U8B_CRC_OFFSET]) !=
      u8b_datagram_crc(datagram, datagram_length))
  {
    return U8B_STATUS_BAD_CRC;
  }
  if (decoded.header.type == U8B_TYPE_DATA)
  {
    status = u8b_validate_records(&datagram[U8B_HEADER_SIZE],
                                  payload_length,
                                  record_count);
    if (status != U8B_STATUS_OK)
    {
      return status;
    }
  }
  else if (record_count != 0U)
  {
    return U8B_STATUS_BAD_RECORD_COUNT;
  }

  decoded.header.session_id = u8b_read_be32(&datagram[8]);
  decoded.header.datagram_seq = u8b_read_be32(&datagram[12]);
  decoded.header.ack_base = u8b_read_be32(&datagram[16]);
  decoded.header.ack_bitmap = u8b_read_be32(&datagram[20]);
  decoded.payload = &datagram[U8B_HEADER_SIZE];
  decoded.payload_len = payload_length;
  decoded.record_count = record_count;
  *view = decoded;
  return U8B_STATUS_OK;
}

u8b_status_t u8b_datagram_record(const u8b_datagram_view_t *datagram,
                                 size_t record_index,
                                 u8b_record_t *record)
{
  size_t offset = 0U;
  size_t index;
  u8b_record_t decoded;
  u8b_status_t status;

  if ((datagram == NULL) || (record == NULL))
  {
    return U8B_STATUS_INVALID_ARGUMENT;
  }
  if (datagram->header.type != U8B_TYPE_DATA)
  {
    return U8B_STATUS_BAD_TYPE;
  }
  if ((datagram->record_count > U8B_MAX_RECORDS) ||
      ((datagram->payload == NULL) && (datagram->payload_len != 0U)))
  {
    return U8B_STATUS_INVALID_ARGUMENT;
  }
  status = u8b_validate_records(datagram->payload,
                                datagram->payload_len,
                                datagram->record_count);
  if (status != U8B_STATUS_OK)
  {
    return status;
  }
  if (record_index >= (size_t)datagram->record_count)
  {
    return U8B_STATUS_BAD_RECORD_COUNT;
  }

  for (index = 0U; index < record_index; ++index)
  {
    const uint16_t data_length =
        u8b_read_be16(&datagram->payload[offset + 2U]);
    offset += U8B_RECORD_HEADER_SIZE + (size_t)data_length;
  }
  decoded.channel_id = datagram->payload[offset];
  decoded.flags = datagram->payload[offset + 1U];
  decoded.data_len = u8b_read_be16(&datagram->payload[offset + 2U]);
  decoded.stream_offset = u8b_read_be32(&datagram->payload[offset + 4U]);
  decoded.data = &datagram->payload[offset + U8B_RECORD_HEADER_SIZE];
  *record = decoded;
  return U8B_STATUS_OK;
}

int u8b_sequence_after(uint32_t candidate, uint32_t reference)
{
  const uint32_t distance = candidate - reference;
  return ((distance != 0U) && (distance < UINT32_C(0x80000000))) ? 1 : 0;
}
