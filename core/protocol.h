#ifndef PROTOCOL_H
#define PROTOCOL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { FRAME_BYTES = 11, FRAME_TIMEOUT_MS = 100 };
enum { TYPE_SAMPLE = 1, TYPE_COMMAND = 2 };
enum { CMD_STATUS = 1, CMD_TEMP_LIMIT, CMD_PRESSURE_LIMIT,
       CMD_STALE_MS, CMD_PROCESS_DELAY, CMD_REPORT_MS, CMD_RESYNC };
typedef struct {
    uint8_t type, channel;
    uint16_t sequence;
    int32_t value;
} frame_t;
typedef struct {
    uint8_t data[FRAME_BYTES];
    size_t length;
    bool active, escaped, discarded;
    uint32_t last_byte;
    uint32_t crc_errors, malformed, timeouts, frames;
} parser_t;
uint16_t crc16(const uint8_t *data, size_t length);

bool parser_feed(parser_t *p, uint8_t byte, uint32_t now_ms, frame_t *out);
void parser_expire(parser_t *p, uint32_t now_ms);
size_t frame_encode(const frame_t *f, uint8_t *out, size_t capacity);
#endif
