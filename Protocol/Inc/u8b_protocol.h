#ifndef U8B_PROTOCOL_H
#define U8B_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define U8B_HEADER_SIZE          32U
#define U8B_RECORD_HEADER_SIZE    8U
#define U8B_MAX_PAYLOAD        1200U
#define U8B_MAX_DATAGRAM       (U8B_HEADER_SIZE + U8B_MAX_PAYLOAD)
#define U8B_MAX_RECORDS           8U
#define U8B_VERSION               1U
#define U8B_MAGIC_U32 UINT32_C(0x55384231)

#define U8B_HEADER_FLAG_MASK      0U
#define U8B_RECORD_FLAG_START     0x01U
#define U8B_RECORD_FLAG_END       0x02U
#define U8B_RECORD_FLAG_MASK      0x03U

typedef enum
{
  U8B_TYPE_DATA = 1,
  U8B_TYPE_ACK = 2,
  U8B_TYPE_HELLO = 3,
  U8B_TYPE_RESET = 4,
  U8B_TYPE_DIAG = 5
} u8b_type_t;

typedef enum
{
  U8B_STATUS_OK = 0,
  U8B_STATUS_INVALID_ARGUMENT,
  U8B_STATUS_OUTPUT_TOO_SMALL,
  U8B_STATUS_TRUNCATED,
  U8B_STATUS_BAD_MAGIC,
  U8B_STATUS_BAD_VERSION,
  U8B_STATUS_BAD_TYPE,
  U8B_STATUS_BAD_HEADER_FLAGS,
  U8B_STATUS_BAD_HEADER_LENGTH,
  U8B_STATUS_BAD_PAYLOAD_LENGTH,
  U8B_STATUS_BAD_RESERVED,
  U8B_STATUS_BAD_RECORD_COUNT,
  U8B_STATUS_BAD_CRC,
  U8B_STATUS_RECORD_TRUNCATED,
  U8B_STATUS_BAD_CHANNEL,
  U8B_STATUS_BAD_RECORD_FLAGS,
  U8B_STATUS_BAD_RECORD_LENGTH,
  U8B_STATUS_TRAILING_DATA
} u8b_status_t;

typedef struct
{
  u8b_type_t type;
  uint8_t flags;
  uint32_t session_id;
  uint32_t datagram_seq;
  uint32_t ack_base;
  uint32_t ack_bitmap;
} u8b_header_t;

typedef struct
{
  uint8_t channel_id;
  uint8_t flags;
  uint16_t data_len;
  uint32_t stream_offset;
  const uint8_t *data;
} u8b_record_t;

typedef struct
{
  u8b_header_t header;
  const uint8_t *payload;
  uint16_t payload_len;
  uint8_t record_count;
} u8b_datagram_view_t;

/* CRC32/ISO-HDLC. A NULL data pointer is valid only when length is zero. */
uint32_t u8b_crc32(const uint8_t *data, size_t length);

/*
 * Encodes DATA and its records into caller-owned storage. Record data remains
 * owned by the caller and must not overlap output. No allocation is performed.
 */
u8b_status_t u8b_encode_data(const u8b_header_t *header,
                             const u8b_record_t *records,
                             size_t record_count,
                             uint8_t *output,
                             size_t output_capacity,
                             size_t *output_length);

/*
 * Encodes ACK/HELLO/RESET/DIAG with an opaque bounded payload. The payload
 * remains caller-owned and must not overlap output. DATA is rejected here.
 */
u8b_status_t u8b_encode_control(const u8b_header_t *header,
                                const uint8_t *payload,
                                size_t payload_length,
                                uint8_t *output,
                                size_t output_capacity,
                                size_t *output_length);

/*
 * Validates the entire datagram, including CRC and every DATA record, before
 * publishing output. The returned view borrows datagram storage; it is valid
 * only while the input bytes remain unchanged and alive.
 */
u8b_status_t u8b_decode_datagram(const uint8_t *datagram,
                                 size_t datagram_length,
                                 u8b_datagram_view_t *view);

/* Returns one already-validated DATA record as a borrowed view. */
u8b_status_t u8b_datagram_record(const u8b_datagram_view_t *datagram,
                                 size_t record_index,
                                 u8b_record_t *record);

/*
 * Wrap-safe serial-number comparison for distances below 2^31. Equal values
 * and the ambiguous distance 2^31 both return zero.
 */
int u8b_sequence_after(uint32_t candidate, uint32_t reference);

#endif /* U8B_PROTOCOL_H */
