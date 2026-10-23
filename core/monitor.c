#include "monitor.h"
#include <string.h>
void monitor_init(monitor_t *m) {
    memset(m, 0, sizeof *m);
    m->threshold[0] = 3000; m->threshold[1] = 12000; m->stale_ms = 500;
}
bool monitor_sample(monitor_t *m, const frame_t *f, uint32_t now) {
    if (f->channel >= CHANNELS) { m->channel_errors++; return false; }
    unsigned ch = f->channel;
    int32_t low = ch == 0 ? -4000 : 0;
    int32_t high = ch == 0 ? 12500 : 30000;
    if (f->value < low || f->value > high) { m->range_errors++; return false; }
    sensor_t *s = &m->sensor[ch];
    if (s->seen) {
        uint16_t delta = (uint16_t)(f->sequence - s->sequence);
        if (!delta) { s->duplicates++; return false; }
        if (delta >= 0x8000u) { s->out_of_order++; return false; }
        s->missing += (uint32_t)delta - 1u;
    }
    s->seen = true; s->sequence = f->sequence; s->value = f->value;
    s->last_ms = now; s->accepted++;

    if (s->value >= m->threshold[ch]) s->warning = true;
    else if (s->value <= m->threshold[ch] - 100) s->warning = false;
    return true;
}
unsigned monitor_state(const monitor_t *m, unsigned ch, uint32_t now) {
    if (ch >= CHANNELS || !m->sensor[ch].seen ||
        (uint32_t)(now - m->sensor[ch].last_ms) >= m->stale_ms) return SENSOR_UNAVAILABLE;
    return m->sensor[ch].warning ? SENSOR_WARNING : SENSOR_NORMAL;
}
