#ifndef U8B_BASIC_PATH_H
#define U8B_BASIC_PATH_H

#include <stddef.h>
#include <stdint.h>

#include "u8b_protocol.h"

#define U8B_BASIC_CHANNEL_COUNT 8U
#define U8B_BASIC_RECORD_DATA_MAX 256U

typedef enum
{
  U8B_BASIC_OK = 0,
  U8B_BASIC_NO_DATA,
  U8B_BASIC_INVALID_ARGUMENT,
  U8B_BASIC_CODEC_ERROR,
  U8B_BASIC_NOT_HELLO,
  U8B_BASIC_PEER_UNKNOWN,
  U8B_BASIC_SESSION_MISMATCH,
  U8B_BASIC_DUPLICATE,
  U8B_BASIC_SEQUENCE_GAP,
  U8B_BASIC_STREAM_DUPLICATE,
  U8B_BASIC_STREAM_GAP,
  U8B_BASIC_STALE_PLAN
} u8b_basic_status_t;

typedef struct
{
  const uint8_t *data;
  uint16_t length;
} u8b_basic_channel_input_t;

typedef struct
{
  uint32_t datagram_sequence;
  uint16_t channel_lengths[U8B_BASIC_CHANNEL_COUNT];
  uint8_t record_count;
  uint8_t next_cursor;
} u8b_basic_tx_plan_t;

typedef struct
{
  uint32_t datagram_sequence;
  uint32_t next_stream_offsets[U8B_BASIC_CHANNEL_COUNT];
  u8b_record_t records[U8B_MAX_RECORDS];
  uint8_t record_count;
} u8b_basic_rx_plan_t;

typedef struct
{
  uint32_t tx_datagrams;
  uint32_t tx_bytes;
  uint32_t rx_datagrams;
  uint32_t rx_bytes;
  uint32_t hello_rx;
  uint32_t peer_session_changes;
  uint32_t codec_errors;
  uint32_t crc_errors;
  uint32_t unknown_session;
  uint32_t session_mismatch;
  uint32_t duplicates;
  uint32_t sequence_gaps;
  uint32_t stream_duplicates;
  uint32_t stream_gaps;
} u8b_basic_counters_t;

typedef struct
{
  uint32_t local_session_id;
  uint32_t peer_session_id;
  uint32_t tx_sequence;
  uint32_t rx_expected_sequence;
  uint32_t tx_stream_offsets[U8B_BASIC_CHANNEL_COUNT];
  uint32_t rx_stream_offsets[U8B_BASIC_CHANNEL_COUNT];
  u8b_basic_counters_t counters;
  uint8_t peer_session_valid;
  uint8_t tx_cursor;
} u8b_basic_path_t;

/* Pure C state machine. Caller owns all packet and channel storage. */
u8b_basic_status_t u8b_basic_path_init(u8b_basic_path_t *path,
                                       uint32_t local_session_id);

u8b_basic_status_t u8b_basic_encode_hello(const u8b_basic_path_t *path,
                                          uint8_t *output,
                                          size_t output_capacity,
                                          size_t *output_length);

/* A new valid HELLO resets only receive-side sequence/stream state. */
u8b_basic_status_t u8b_basic_accept_hello(u8b_basic_path_t *path,
                                          const uint8_t *datagram,
                                          size_t datagram_length,
                                          uint8_t *peer_changed);

/* Encode is non-destructive. Commit only after UDP accepted the datagram. */
u8b_basic_status_t u8b_basic_prepare_tx(
  const u8b_basic_path_t *path,
  const u8b_basic_channel_input_t inputs[U8B_BASIC_CHANNEL_COUNT],
  uint8_t *output,
  size_t output_capacity,
  size_t *output_length,
  u8b_basic_tx_plan_t *plan);
u8b_basic_status_t u8b_basic_commit_tx(u8b_basic_path_t *path,
                                       const u8b_basic_tx_plan_t *plan);

/* Decode/validate is non-destructive. Commit only after all UART TX writes. */
u8b_basic_status_t u8b_basic_prepare_rx(u8b_basic_path_t *path,
                                        const uint8_t *datagram,
                                        size_t datagram_length,
                                        u8b_basic_rx_plan_t *plan);
u8b_basic_status_t u8b_basic_commit_rx(u8b_basic_path_t *path,
                                       const u8b_basic_rx_plan_t *plan);

#endif /* U8B_BASIC_PATH_H */
