#include "proto_event.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "proto_event_caps.h"
#include "proto_hex.h"

void proto_event_init(proto_event_t *event, proto_protocol_t protocol, proto_direction_t direction)
{
    if (!event) { return; }
    memset(event, 0, sizeof(*event));
    event->protocol = protocol;
    event->direction = direction;
}

const char *proto_event_check_name(proto_event_check_t check)
{
    switch (check) {
    case PROTO_EVENT_OK: return "ok";
    case PROTO_EVENT_ERR_NULL: return "null";
    case PROTO_EVENT_ERR_PROTOCOL: return "bad_protocol";
    case PROTO_EVENT_ERR_DIRECTION: return "bad_direction";
    case PROTO_EVENT_ERR_STATUS: return "bad_status";
    case PROTO_EVENT_ERR_LENGTH: return "bad_length";
    case PROTO_EVENT_ERR_METADATA: return "bad_metadata";
    case PROTO_EVENT_ERR_HOPS: return "too_many_hops";
    }
    return "?";
}

const char *proto_event_status_name(proto_event_status_t status)
{
    switch (status) {
    case PROTO_EVENT_STATUS_OK: return "OK";
    case PROTO_EVENT_STATUS_TIMEOUT: return "TIMEOUT";
    case PROTO_EVENT_STATUS_NACK: return "NACK";
    case PROTO_EVENT_STATUS_BUS_ERROR: return "BUS_ERROR";
    case PROTO_EVENT_STATUS_INVALID: return "INVALID";
    case PROTO_EVENT_STATUS_REJECTED: return "REJECTED";
    }
    return "?";
}

static bool serial_terminator_consistent(const proto_event_t *event)
{
    const uint8_t *p = event->payload;
    const size_t n = event->payload_length;
    switch (event->meta.serial.terminator) {
    case PROTO_TERM_NONE: return true;
    case PROTO_TERM_LF: return n >= 1 && p[n - 1] == '\n' && !(n >= 2 && p[n - 2] == '\r');
    case PROTO_TERM_CRLF: return n >= 2 && p[n - 2] == '\r' && p[n - 1] == '\n';
    }
    return false;
}

static proto_event_check_t validate_meta(const proto_event_t *event)
{
    const bool is_tx = event->direction == PROTO_DIR_TX;
    const bool ok = event->status == PROTO_EVENT_STATUS_OK;
    const size_t n = event->payload_length;
    switch (event->protocol) {
    case PROTO_PROTOCOL_UART:
    case PROTO_PROTOCOL_RS485: {
        const proto_serial_meta_t *m = &event->meta.serial;
        if ((unsigned)m->terminator > PROTO_TERM_CRLF) { return PROTO_EVENT_ERR_METADATA; }
        if (m->baud_rate == 0 && !is_tx) { return PROTO_EVENT_ERR_METADATA; }
        if (n == 0 && ok) { return PROTO_EVENT_ERR_LENGTH; }
        if (!serial_terminator_consistent(event)) { return PROTO_EVENT_ERR_METADATA; }
        return PROTO_EVENT_OK;
    }
    case PROTO_PROTOCOL_CAN: {
        const proto_can_meta_t *m = &event->meta.can;
        proto_can_frame_t frame = { .id = m->id, .extended = m->extended, .rtr = m->rtr,
                                    .dlc = m->dlc };
        if (!proto_can_frame_valid(&frame)) { return PROTO_EVENT_ERR_METADATA; }
        if (n != (m->rtr ? 0u : m->dlc)) { return PROTO_EVENT_ERR_LENGTH; }
        return PROTO_EVENT_OK;
    }
    case PROTO_PROTOCOL_I2C: {
        const proto_i2c_meta_t *m = &event->meta.i2c;
        proto_i2c_txn_t txn = { .address = m->address, .op = m->op,
                                .write_length = m->write_length, .read_length = m->read_length };
        if (!proto_i2c_txn_valid(&txn) || (unsigned)m->ack > PROTO_I2C_ACK_NACK) {
            return PROTO_EVENT_ERR_METADATA;
        }
        if (is_tx) { return n == m->write_length ? PROTO_EVENT_OK : PROTO_EVENT_ERR_LENGTH; }
        if (ok) { return n == m->read_length ? PROTO_EVENT_OK : PROTO_EVENT_ERR_LENGTH; }
        return n == 0 ? PROTO_EVENT_OK : PROTO_EVENT_ERR_LENGTH;
    }
    case PROTO_PROTOCOL_SPI: {
        const proto_spi_meta_t *m = &event->meta.spi;
        if (!proto_spi_mode_valid(m->mode) || m->chip_select >= PROTO_SPI_CS_COUNT ||
            m->word_bits != 8 || !proto_spi_transfer_length_valid(m->transaction_length) ||
            (m->clock_hz != 0 && !proto_spi_clock_valid(m->clock_hz))) {
            return PROTO_EVENT_ERR_METADATA;
        }
        if (!is_tx && !ok) { return n == 0 ? PROTO_EVENT_OK : PROTO_EVENT_ERR_LENGTH; }
        return n == m->transaction_length ? PROTO_EVENT_OK : PROTO_EVENT_ERR_LENGTH;
    }
    case PROTO_PROTOCOL_COUNT:
        break;
    }
    return PROTO_EVENT_ERR_PROTOCOL;
}

proto_event_check_t proto_event_validate(const proto_event_t *event)
{
    if (!event) { return PROTO_EVENT_ERR_NULL; }
    if ((unsigned)event->protocol >= PROTO_PROTOCOL_COUNT) { return PROTO_EVENT_ERR_PROTOCOL; }
    if ((unsigned)event->direction > PROTO_DIR_TX) { return PROTO_EVENT_ERR_DIRECTION; }
    if ((unsigned)event->status > PROTO_EVENT_STATUS_REJECTED) { return PROTO_EVENT_ERR_STATUS; }
    if (event->payload_length > PROTO_EVENT_MAX_PAYLOAD ||
        event->payload_length > proto_protocol_max_payload(event->protocol)) {
        return PROTO_EVENT_ERR_LENGTH;
    }
    if (event->hop_count > PROTO_EVENT_MAX_HOPS) { return PROTO_EVENT_ERR_HOPS; }
    return validate_meta(event);
}

/* ---- CAN ------------------------------------------------------------------ */
bool proto_event_from_can(proto_event_t *event, const proto_can_frame_t *frame,
                          proto_direction_t direction)
{
    if (!event || !proto_can_frame_valid(frame)) { return false; }
    proto_event_init(event, PROTO_PROTOCOL_CAN, direction);
    event->timestamp_us = frame->timestamp_us;
    event->meta.can.id = frame->id;
    event->meta.can.extended = frame->extended;
    event->meta.can.rtr = frame->rtr;
    event->meta.can.dlc = frame->dlc;
    if (!frame->rtr) {
        memcpy(event->payload, frame->data, frame->dlc);
        event->payload_length = frame->dlc;
    }
    return true;
}

bool proto_event_to_can(const proto_event_t *event, proto_can_frame_t *frame)
{
    if (!event || !frame || event->protocol != PROTO_PROTOCOL_CAN ||
        proto_event_validate(event) != PROTO_EVENT_OK) {
        return false;
    }
    memset(frame, 0, sizeof(*frame));
    frame->id = event->meta.can.id;
    frame->extended = event->meta.can.extended;
    frame->rtr = event->meta.can.rtr;
    frame->dlc = event->meta.can.dlc;
    frame->timestamp_us = event->timestamp_us;
    if (!frame->rtr) { memcpy(frame->data, event->payload, frame->dlc); }
    return true;
}

/* ---- UART / RS-485 ---------------------------------------------------------- */
bool proto_event_from_serial(proto_event_t *event, proto_protocol_t protocol,
                             const proto_uart_frame_t *frame, uint32_t baud_rate,
                             proto_direction_t direction)
{
    if (!event || !frame || (protocol != PROTO_PROTOCOL_UART && protocol != PROTO_PROTOCOL_RS485) ||
        frame->length == 0 || frame->length > PROTO_UART_FRAME_MAX) {
        return false;
    }
    proto_event_init(event, protocol, direction);
    event->timestamp_us = frame->completed_us;
    memcpy(event->payload, frame->data, frame->length);
    event->payload_length = (uint16_t)frame->length;
    event->meta.serial.baud_rate = baud_rate;
    if (frame->length >= 2 && frame->data[frame->length - 2] == '\r' &&
        frame->data[frame->length - 1] == '\n') {
        event->meta.serial.terminator = PROTO_TERM_CRLF;
    } else if (frame->data[frame->length - 1] == '\n') {
        event->meta.serial.terminator = PROTO_TERM_LF;
    } else {
        event->meta.serial.terminator = PROTO_TERM_NONE;
    }
    return true;
}

bool proto_event_to_serial(const proto_event_t *event, uint8_t *out, size_t out_size,
                           size_t *length)
{
    if (!event || !out || !length ||
        (event->protocol != PROTO_PROTOCOL_UART && event->protocol != PROTO_PROTOCOL_RS485) ||
        proto_event_validate(event) != PROTO_EVENT_OK || event->payload_length > out_size) {
        return false;
    }
    memcpy(out, event->payload, event->payload_length);
    *length = event->payload_length;
    return true;
}

/* ---- I2C -------------------------------------------------------------------- */
bool proto_event_from_i2c_request(proto_event_t *event, const proto_i2c_txn_t *txn)
{
    if (!event || !proto_i2c_txn_valid(txn)) { return false; }
    proto_event_init(event, PROTO_PROTOCOL_I2C, PROTO_DIR_TX);
    event->meta.i2c.address = txn->address;
    event->meta.i2c.op = txn->op;
    event->meta.i2c.write_length = txn->write_length;
    event->meta.i2c.read_length = txn->read_length;
    event->meta.i2c.ack = PROTO_I2C_ACK_UNKNOWN;
    memcpy(event->payload, txn->write_data, txn->write_length);
    event->payload_length = txn->write_length;
    return true;
}

bool proto_event_to_i2c(const proto_event_t *event, proto_i2c_txn_t *txn)
{
    if (!event || !txn || event->protocol != PROTO_PROTOCOL_I2C ||
        event->direction != PROTO_DIR_TX || proto_event_validate(event) != PROTO_EVENT_OK) {
        return false;
    }
    memset(txn, 0, sizeof(*txn));
    txn->address = event->meta.i2c.address;
    txn->op = event->meta.i2c.op;
    txn->write_length = event->meta.i2c.write_length;
    txn->read_length = event->meta.i2c.read_length;
    memcpy(txn->write_data, event->payload, event->payload_length);
    return true;
}

bool proto_event_i2c_result(proto_event_t *result, const proto_event_t *request,
                            const uint8_t *read_data, proto_event_status_t status,
                            int32_t error_code, uint64_t timestamp_us)
{
    if (!result || !request || request->protocol != PROTO_PROTOCOL_I2C) { return false; }
    proto_event_t out;
    proto_event_init(&out, PROTO_PROTOCOL_I2C, PROTO_DIR_RX);
    out.correlation_id = request->event_id;
    out.device_id = request->device_id;
    out.status = status;
    out.error_code = error_code;
    out.timestamp_us = timestamp_us;
    out.hop_count = request->hop_count;
    out.origin_route_id = request->origin_route_id;
    out.from_protoscope = false;
    out.meta.i2c = request->meta.i2c;
    out.meta.i2c.ack = status == PROTO_EVENT_STATUS_OK ? PROTO_I2C_ACK_ACK :
                       status == PROTO_EVENT_STATUS_NACK ? PROTO_I2C_ACK_NACK :
                       PROTO_I2C_ACK_UNKNOWN;
    if (status == PROTO_EVENT_STATUS_OK && out.meta.i2c.read_length) {
        if (!read_data) { return false; }
        memcpy(out.payload, read_data, out.meta.i2c.read_length);
        out.payload_length = out.meta.i2c.read_length;
    }
    *result = out;
    return true;
}

/* ---- SPI -------------------------------------------------------------------- */
bool proto_event_from_spi_request(proto_event_t *event, const uint8_t *tx, size_t length,
                                  uint8_t mode, uint32_t clock_hz)
{
    if (!event || !tx || !proto_spi_transfer_length_valid(length) || !proto_spi_mode_valid(mode) ||
        (clock_hz != 0 && !proto_spi_clock_valid(clock_hz))) {
        return false;
    }
    proto_event_init(event, PROTO_PROTOCOL_SPI, PROTO_DIR_TX);
    event->meta.spi.chip_select = 0;
    event->meta.spi.mode = mode;
    event->meta.spi.clock_hz = clock_hz;
    event->meta.spi.word_bits = 8;
    event->meta.spi.transaction_length = (uint16_t)length;
    memcpy(event->payload, tx, length);
    event->payload_length = (uint16_t)length;
    return true;
}

bool proto_event_to_spi(const proto_event_t *event, uint8_t *tx, size_t tx_size, size_t *length)
{
    if (!event || !tx || !length || event->protocol != PROTO_PROTOCOL_SPI ||
        event->direction != PROTO_DIR_TX || proto_event_validate(event) != PROTO_EVENT_OK ||
        event->payload_length > tx_size) {
        return false;
    }
    memcpy(tx, event->payload, event->payload_length);
    *length = event->payload_length;
    return true;
}

bool proto_event_spi_result(proto_event_t *result, const proto_event_t *request,
                            const uint8_t *rx_data, uint8_t mode, uint32_t clock_hz,
                            proto_event_status_t status, int32_t error_code,
                            uint64_t timestamp_us)
{
    if (!result || !request || request->protocol != PROTO_PROTOCOL_SPI) { return false; }
    proto_event_t out;
    proto_event_init(&out, PROTO_PROTOCOL_SPI, PROTO_DIR_RX);
    out.correlation_id = request->event_id;
    out.device_id = request->device_id;
    out.status = status;
    out.error_code = error_code;
    out.timestamp_us = timestamp_us;
    out.hop_count = request->hop_count;
    out.origin_route_id = request->origin_route_id;
    out.meta.spi = request->meta.spi;
    out.meta.spi.mode = mode;       /* the values actually used, not the request's 0 = default */
    out.meta.spi.clock_hz = clock_hz;
    if (status == PROTO_EVENT_STATUS_OK) {
        if (!rx_data) { return false; }
        memcpy(out.payload, rx_data, out.meta.spi.transaction_length);
        out.payload_length = out.meta.spi.transaction_length;
    }
    *result = out;
    return true;
}

/* ---- provenance --------------------------------------------------------------- */
void proto_event_mark_forwarded(proto_event_t *out, const proto_event_t *source, uint8_t route_id)
{
    if (!out || !source) { return; }
    out->hop_count = (uint8_t)(source->hop_count + 1u);
    out->origin_route_id = route_id;
    out->from_protoscope = true;
    out->correlation_id = source->event_id;
}

bool proto_event_loop_rejected(const proto_event_t *event)
{
    return !event || event->from_protoscope || event->hop_count >= PROTO_EVENT_MAX_HOPS;
}

/* ---- text form ------------------------------------------------------------------ */
size_t proto_event_format(const proto_event_t *event, char *out, size_t out_size)
{
    if (!out || out_size == 0) { return 0; }
    out[0] = '\0';
    if (!event) { return 0; }
    char hex[2 * PROTO_EVENT_MAX_PAYLOAD + 1] = "";
    (void)proto_hex_format(event->payload, event->payload_length, hex, sizeof(hex));
    char meta[96] = "";
    switch (event->protocol) {
    case PROTO_PROTOCOL_UART:
    case PROTO_PROTOCOL_RS485:
        snprintf(meta, sizeof(meta), "baud=%" PRIu32 " term=%s", event->meta.serial.baud_rate,
                 event->meta.serial.terminator == PROTO_TERM_CRLF ? "CRLF" :
                 event->meta.serial.terminator == PROTO_TERM_LF ? "LF" : "NONE");
        break;
    case PROTO_PROTOCOL_CAN:
        snprintf(meta, sizeof(meta), "id=%" PRIX32 " ext=%d rtr=%d dlc=%u", event->meta.can.id,
                 event->meta.can.extended, event->meta.can.rtr, (unsigned)event->meta.can.dlc);
        break;
    case PROTO_PROTOCOL_I2C:
        snprintf(meta, sizeof(meta), "addr=0x%02X op=%s wr=%u rd=%u ack=%s",
                 (unsigned)event->meta.i2c.address,
                 event->meta.i2c.op == PROTO_I2C_OP_WRITE ? "WRITE" :
                 event->meta.i2c.op == PROTO_I2C_OP_READ ? "READ" : "WRITE_READ",
                 (unsigned)event->meta.i2c.write_length, (unsigned)event->meta.i2c.read_length,
                 event->meta.i2c.ack == PROTO_I2C_ACK_ACK ? "ACK" :
                 event->meta.i2c.ack == PROTO_I2C_ACK_NACK ? "NACK" : "?");
        break;
    case PROTO_PROTOCOL_SPI:
        snprintf(meta, sizeof(meta), "cs=%u mode=%u clock_hz=%" PRIu32 " bits=%u len=%u",
                 (unsigned)event->meta.spi.chip_select, (unsigned)event->meta.spi.mode,
                 event->meta.spi.clock_hz, (unsigned)event->meta.spi.word_bits,
                 (unsigned)event->meta.spi.transaction_length);
        break;
    case PROTO_PROTOCOL_COUNT:
        break;
    }
    const int written = snprintf(out, out_size,
        "event id=%" PRIu64 " corr=%" PRIu64 " proto=%s dev=%u dir=%s status=%s err=%d"
        " t_us=%" PRIu64 " hop=%u origin=%u own=%d len=%u data=%s | %s",
        event->event_id, event->correlation_id, proto_protocol_name(event->protocol),
        (unsigned)event->device_id, event->direction == PROTO_DIR_TX ? "TX" : "RX",
        proto_event_status_name(event->status), (int)event->error_code, event->timestamp_us,
        (unsigned)event->hop_count, (unsigned)event->origin_route_id, event->from_protoscope,
        (unsigned)event->payload_length, hex, meta);
    if (written < 0) { out[0] = '\0'; return 0; }
    return (size_t)written < out_size ? (size_t)written : out_size - 1;
}
