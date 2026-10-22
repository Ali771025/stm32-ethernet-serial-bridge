#ifndef U8B_RELIABLE_H
#define U8B_RELIABLE_H

#include <stddef.h>
#include <stdint.h>

#include "u8b_protocol.h"

#define U8B_RELIABLE_CHANNEL_COUNT 8U
#define U8B_RELIABLE_WINDOW_SIZE 32U
#define U8B_RELIABLE_RECORD_DATA_MAX 256U
#define U8B_RELIABLE_INITIAL_RTO_MS 20U
#define U8B_RELIABLE_MAX_RTO_MS 200U
#define U8B_RELIABLE_MAX_RETRIES 5U

typedef enum
{
  U8B_RELIABLE_OK = 0,
  U8B_RELIABLE_NO_DATA,
  U8B_RELIABLE_NO_WORK,
  U8B_RELIABLE_INVALID_ARGUMENT,
  U8B_RELIABLE_CODEC_ERROR,
  U8B_RELIABLE_PEER_UNKNOWN,
  U8B_RELIABLE_SESSION_MISMATCH,
  U8B_RELIABLE_WINDOW_FULL,
  U8B_RELIABLE_DUPLICATE,
  U8B_RELIABLE_OUTSIDE_WINDOW,
  U8B_RELIABLE_STREAM_ERROR,
  U8B_RELIABLE_STALE_TOKEN
} u8b_reliable_status_t;

typedef struct
{
  const uint8_t *data;
  uint16_t length;
} u8b_reliable_channel_input_t;

typedef struct
{
  uint32_t sequence;
  uint16_t channel_lengths[U8B_RELIABLE_CHANNEL_COUNT];
  uint8_t record_count;
} u8b_reliable_queue_result_t;

typedef struct
{
  const uint8_t *data;
  uint16_t length;
  uint32_t sequence;
  uint8_t is_retry;
} u8b_reliable_tx_view_t;

typedef struct
{
  uint32_t sequence;
  u8b_record_t records[U8B_MAX_RECORDS];
  uint8_t record_count;
  uint8_t slot_index;
} u8b_reliable_delivery_t;

typedef struct
{
  uint32_t queued_datagrams;
  uint32_t queued_bytes;
  uint32_t initial_transmissions;
  uint32_t retransmissions;
  uint32_t delivery_failures;
  uint32_t acknowledgements_rx;
  uint32_t acknowledged_datagrams;
  uint32_t data_rx;
  uint32_t data_rx_bytes;
  uint32_t acknowledgements_tx;
  uint32_t duplicates;
  uint32_t outside_window;
  uint32_t reordered;
  uint32_t crc_errors;
  uint32_t codec_errors;
  uint32_t session_mismatches;
  uint32_t peer_session_changes;
  uint32_t stream_errors;
  uint32_t delivered_datagrams;
  uint32_t delivered_bytes;
  uint32_t tx_window_high_water;
  uint32_t rx_window_high_water;
  uint8_t degraded;
} u8b_reliable_counters_t;

typedef struct
{
  uint8_t data[U8B_MAX_DATAGRAM];
  uint32_t sequence;
  uint32_t last_send_ms;
  uint16_t length;
  uint8_t occupied;
  uint8_t sent;
  uint8_t retry_count;
  uint8_t exhausted;
} u8b_reliable_tx_slot_t;

typedef struct
{
  uint8_t data[U8B_MAX_DATAGRAM];
  uint32_t sequence;
  uint16_t length;
  uint8_t occupied;
  uint8_t accepted;
} u8b_reliable_rx_slot_t;

typedef struct
{
  uint32_t local_session_id;
  uint32_t peer_session_id;
  uint32_t tx_next_sequence;
  uint32_t rx_ack_base;
  uint32_t rx_deliver_next;
  uint32_t tx_stream_offsets[U8B_RELIABLE_CHANNEL_COUNT];
  uint32_t rx_accept_offsets[U8B_RELIABLE_CHANNEL_COUNT];
  u8b_reliable_tx_slot_t tx_slots[U8B_RELIABLE_WINDOW_SIZE];
  u8b_reliable_rx_slot_t rx_slots[U8B_RELIABLE_WINDOW_SIZE];
  u8b_reliable_counters_t counters;
  uint8_t peer_session_valid;
  uint8_t tx_cursor;
  uint8_t tx_count;
  uint8_t rx_count;
  uint8_t ack_pending;
  uint8_t rx_offset_known[U8B_RELIABLE_CHANNEL_COUNT];
} u8b_reliable_t;

u8b_reliable_status_t u8b_reliable_init(u8b_reliable_t *state,
                                        uint32_t local_session_id);
u8b_reliable_status_t u8b_reliable_encode_hello(const u8b_reliable_t *state,
                                                uint8_t *output,
                                                size_t capacity,
                                                size_t *length);
u8b_reliable_status_t u8b_reliable_accept_hello(u8b_reliable_t *state,
                                                const uint8_t *datagram,
                                                size_t length,
                                                uint8_t *peer_changed);

u8b_reliable_status_t u8b_reliable_queue_data(
  u8b_reliable_t *state,
  const u8b_reliable_channel_input_t inputs[U8B_RELIABLE_CHANNEL_COUNT],
  u8b_reliable_queue_result_t *result);
u8b_reliable_status_t u8b_reliable_next_tx(u8b_reliable_t *state,
                                           uint32_t now_ms,
                                           u8b_reliable_tx_view_t *view);
u8b_reliable_status_t u8b_reliable_commit_tx(u8b_reliable_t *state,
                                             uint32_t sequence,
                                             uint32_t now_ms);

u8b_reliable_status_t u8b_reliable_accept_data(u8b_reliable_t *state,
                                               const uint8_t *datagram,
                                               size_t length);
u8b_reliable_status_t u8b_reliable_encode_ack(u8b_reliable_t *state,
                                              uint8_t *output,
                                              size_t capacity,
                                              size_t *length);
u8b_reliable_status_t u8b_reliable_commit_ack(u8b_reliable_t *state);
u8b_reliable_status_t u8b_reliable_accept_ack(u8b_reliable_t *state,
                                              const uint8_t *datagram,
                                              size_t length);

u8b_reliable_status_t u8b_reliable_peek_delivery(
  u8b_reliable_t *state,
  u8b_reliable_delivery_t *delivery);
u8b_reliable_status_t u8b_reliable_commit_delivery(
  u8b_reliable_t *state,
  const u8b_reliable_delivery_t *delivery);

uint8_t u8b_reliable_has_due_tx(u8b_reliable_t *state, uint32_t now_ms);
uint8_t u8b_reliable_has_delivery(const u8b_reliable_t *state);
uint8_t u8b_reliable_ack_pending(const u8b_reliable_t *state);

#endif /* U8B_RELIABLE_H */
