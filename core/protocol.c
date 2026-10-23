#include "protocol.h"

uint16_t crc16(const uint8_t *data, size_t length) {
    uint16_t crc = 0xffffu;
    for (size_t i = 0; i < length; ++i) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (unsigned j = 0; j < 8; ++j) {
            crc = (uint16_t)((crc & 0x8000u) ?
                  ((uint32_t)crc << 1) ^ 0x1021u : (uint32_t)crc << 1);
        }
    }
    return crc;
}

void parser_expire(parser_t *p, uint32_t now) {
    if (p->active && (p->length || p->escaped || p->discarded) &&
        (uint32_t)(now - p->last_byte) >= FRAME_TIMEOUT_MS) {
        p->timeouts++;
        p->active = false;
        p->length = 0;
        p->escaped = p->discarded = false;
    }
}

bool parser_feed(parser_t *p, uint8_t b, uint32_t now, frame_t *out) {
    parser_expire(p, now);
    p->last_byte = now;
    if (b == 0x7eu) {
        bool valid = false;
        if (p->active && (p->length || p->escaped || p->discarded)) {
            if (p->discarded) {   }
            else if (p->escaped || p->length != FRAME_BYTES) p->malformed++;
            else {
                uint16_t expected = (uint16_t)((uint16_t)p->data[9] << 8) | p->data[10];
                if (crc16(p->data, 9) != expected) p->crc_errors++;
                else if (p->data[0] != 1 ||
                         (p->data[1] != TYPE_SAMPLE && p->data[1] != TYPE_COMMAND)) {
                    p->malformed++;
                } else {
                    out->type = p->data[1]; out->channel = p->data[2];
                    out->sequence = (uint16_t)((uint16_t)p->data[3] << 8) | p->data[4];
                    uint32_t u = ((uint32_t)p->data[5] << 24) |
                                 ((uint32_t)p->data[6] << 16) |
                                 ((uint32_t)p->data[7] << 8) | p->data[8];

                    out->value = u <= INT32_MAX ? (int32_t)u : -1 - (int32_t)(~u);
                    p->frames++; valid = true;
                }
            }
        }
        p->active = true; p->length = 0;
        p->escaped = p->discarded = false;
        return valid;
    }
    if (!p->active || p->discarded) return false;
    if (p->escaped) { b ^= 0x20u; p->escaped = false; }
    else if (b == 0x7du) { p->escaped = true; return false; }
    if (p->length == FRAME_BYTES) {
        p->discarded = true; p->malformed++; return false;
    }
    p->data[p->length++] = b;
    return false;
}

size_t frame_encode(const frame_t *f, uint8_t *out, size_t capacity) {
    uint32_t v = (uint32_t)f->value;
    uint8_t d[FRAME_BYTES] = {1, f->type, f->channel,
        (uint8_t)(f->sequence >> 8), (uint8_t)f->sequence,
        (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v, 0, 0};
    uint16_t c = crc16(d, 9); d[9] = (uint8_t)(c >> 8); d[10] = (uint8_t)c;
    size_t need = 2;
    for (size_t i = 0; i < sizeof d; ++i) need += (d[i] == 0x7e || d[i] == 0x7d) ? 2u : 1u;
    if (capacity < need) return 0;
    size_t n = 0; out[n++] = 0x7e;
    for (size_t i = 0; i < sizeof d; ++i) {
        if (d[i] == 0x7e || d[i] == 0x7d) { out[n++] = 0x7d; out[n++] = d[i] ^ 0x20u; }
        else out[n++] = d[i];
    }
    out[n++] = 0x7e;
    return n;
}
