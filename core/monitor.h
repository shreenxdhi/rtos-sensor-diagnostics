#ifndef MONITOR_H
#define MONITOR_H
#include "protocol.h"
enum { CHANNELS = 2 };
enum { SENSOR_UNAVAILABLE, SENSOR_NORMAL, SENSOR_WARNING };
typedef struct {
    bool seen, warning;
    uint16_t sequence;
    int32_t value;
    uint32_t last_ms, accepted, duplicates, out_of_order, missing, resyncs;
} sensor_t;
typedef struct {
    sensor_t sensor[CHANNELS];
    int32_t threshold[CHANNELS];
    uint32_t stale_ms, range_errors, channel_errors;
} monitor_t;
void monitor_init(monitor_t *m);
bool monitor_resync(monitor_t *m, unsigned channel);
bool monitor_sample(monitor_t *m, const frame_t *f, uint32_t now);
unsigned monitor_state(const monitor_t *m, unsigned ch, uint32_t now);
#endif
