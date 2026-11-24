#pragma once

/* Pure-C log file format (Phase 8, schema version 1). No ESP-IDF dependency. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "proto_event.h"

#define PROTO_LOG_SCHEMA_VERSION 1
#define PROTO_LOG_LINE_MAX 448
#define PROTO_LOG_NAME_MAX 13          /* "LOG0001.CSV" + NUL: FAT 8.3 */
#define PROTO_LOG_INDEX_MAX 9999

#define PROTO_LOG_MASK_ALL ((1u << PROTO_PROTOCOL_COUNT) - 1u)
#define PROTO_LOG_MASK(p) (1u << (unsigned)(p))

/* "# ProtoScope log schema=1 ..." comment lines then the CSV column row. */
size_t proto_log_format_header(const char *firmware, char *out, size_t out_size);
/* One CSV line ending in LF; 0 if the event is invalid or out is too small. Columns:
 * timestamp_us,event_id,correlation_id,protocol,direction,status,error_code,hop,
 * origin_route,length,data_hex,meta   (meta uses ';' so a row never needs quoting). */
size_t proto_log_format_record(const proto_event_t *event, char *out, size_t out_size);
/* "LOG0007.CSV" (case-insensitive) -> 7. */
bool proto_log_parse_index(const char *name, uint16_t *index);
void proto_log_make_name(uint16_t index, char *out, size_t out_size);
