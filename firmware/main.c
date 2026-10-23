#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "bsp.h"
#include "monitor.h"

enum { RX_CAPACITY = 256, SAMPLE_CAPACITY = 8, EVENT_CAPACITY = 16 };
typedef struct { frame_t frame; uint32_t arrival_ms; } sample_t;
typedef struct { unsigned kind; uint16_t token; uint8_t command, channel; int32_t value; } event_t;
enum { EVENT_ACK, EVENT_STATUS, EVENT_STATE };
static QueueHandle_t rx_queue, sample_queue, event_queue;
static TaskHandle_t parser_handle, monitor_handle, reporter_handle;
static parser_t parser;
static monitor_t monitor;
static uint32_t rx_drops, sample_drops, event_drops, queue_peak;
static uint32_t process_delay_ms, report_ms = 200;

typedef struct { char data[1024]; size_t length; } writer_t;
static void text(writer_t *w, const char *s) {
    while (*s && w->length < sizeof w->data - 1) w->data[w->length++] = *s++;
    w->data[w->length] = 0;
}
static void number(writer_t *w, uint32_t n) {
    char rev[10]; unsigned i = 0;
    do { rev[i++] = (char)('0' + n % 10u); n /= 10u; } while (n);
    while (i) { char s[2] = {rev[--i], 0}; text(w, s); }
}
static void signed_number(writer_t *w, int32_t n) {
    if (n < 0) { text(w, "-"); number(w, 0u - (uint32_t)n); }
    else number(w, (uint32_t)n);
}
static void field(writer_t *w, const char *name, uint32_t n) {
    text(w, ",\""); text(w, name); text(w, "\":"); number(w, n);
}
static void send_event(event_t e) {
    if (xQueueSend(event_queue, &e, 0) != pdPASS) {
        taskENTER_CRITICAL(); event_drops++; taskEXIT_CRITICAL();
    }
}

void uart_rx_byte(uint8_t b) {
    BaseType_t wake = pdFALSE;
    if (xQueueSendFromISR(rx_queue, &b, &wake) != pdPASS) rx_drops++;
    portYIELD_FROM_ISR(wake);
}

static void command(const frame_t *f) {
    bool ok = false;
    taskENTER_CRITICAL();
    switch (f->channel) {
    case CMD_STATUS: ok = f->value == 0; break;
    case CMD_TEMP_LIMIT:
        if (f->value >= -3900 && f->value <= 12500) {
            monitor.threshold[0] = f->value;
            monitor.sensor[0].warning = monitor.sensor[0].value >= f->value;
            ok = true;
        } break;
    case CMD_PRESSURE_LIMIT:
        if (f->value >= 100 && f->value <= 30000) {
            monitor.threshold[1] = f->value;
            monitor.sensor[1].warning = monitor.sensor[1].value >= f->value;
            ok = true;
        } break;
    case CMD_STALE_MS:
        if (f->value >= 100 && f->value <= 5000) { monitor.stale_ms = (uint32_t)f->value; ok = true; } break;
    case CMD_PROCESS_DELAY:
        if (f->value >= 0 && f->value <= 200) { process_delay_ms = (uint32_t)f->value; ok = true; } break;
    case CMD_REPORT_MS:
        if (f->value >= 100 && f->value <= 5000) { report_ms = (uint32_t)f->value; ok = true; } break;
    default: break;
    }
    taskEXIT_CRITICAL();
    send_event((event_t){EVENT_ACK, f->sequence, f->channel, 0, ok ? 1 : 0});
    if (ok && f->channel == CMD_STATUS) send_event((event_t){EVENT_STATUS, f->sequence, 0, 0, 0});
}

static void parser_task(void *arg) {
    (void)arg;
    uart_enable_rx_irq();
    for (;;) {
        uint8_t b;
        if (xQueueReceive(rx_queue, &b, pdMS_TO_TICKS(20)) == pdPASS) {
            frame_t f;
            uint32_t now = xTaskGetTickCount();
            taskENTER_CRITICAL();
            bool complete = parser_feed(&parser, b, now, &f);
            taskEXIT_CRITICAL();
            if (complete) {
                if (f.type == TYPE_COMMAND) command(&f);
                else {
                    sample_t item = {f, now};
                    if (xQueueSend(sample_queue, &item, 0) != pdPASS) {
                        taskENTER_CRITICAL(); sample_drops++; taskEXIT_CRITICAL();
                    } else {
                        uint32_t depth = (uint32_t)uxQueueMessagesWaiting(sample_queue);
                        taskENTER_CRITICAL();
                        if (depth > queue_peak) queue_peak = depth;
                        taskEXIT_CRITICAL();
                    }
                }
            }
        } else {
            taskENTER_CRITICAL(); parser_expire(&parser, xTaskGetTickCount()); taskEXIT_CRITICAL();
        }
    }
}

static void monitor_task(void *arg) {
    (void)arg;
    unsigned prior[CHANNELS] = {SENSOR_UNAVAILABLE, SENSOR_UNAVAILABLE};
    for (;;) {
        sample_t item;
        if (xQueueReceive(sample_queue, &item, pdMS_TO_TICKS(20)) == pdPASS) {
            taskENTER_CRITICAL();
            (void)monitor_sample(&monitor, &item.frame, item.arrival_ms);
            uint32_t delay = process_delay_ms;
            taskEXIT_CRITICAL();

            if (delay) vTaskDelay(pdMS_TO_TICKS(delay));
        }
        for (unsigned ch = 0; ch < CHANNELS; ++ch) {
            taskENTER_CRITICAL(); unsigned state = monitor_state(&monitor, ch, xTaskGetTickCount()); taskEXIT_CRITICAL();
            if (state != prior[ch]) {
                send_event((event_t){EVENT_STATE, 0, 0, (uint8_t)ch, (int32_t)state});
                prior[ch] = state;
            }
        }
    }
}

static const char *state_name(unsigned state) {
    return state == SENSOR_NORMAL ? "normal" : state == SENSOR_WARNING ? "warning" : "unavailable";
}

static void status(uint16_t token) {
    monitor_t m; parser_t p;
    uint32_t rx, dropped, logs, peak, delay, interval, now;

    taskENTER_CRITICAL();
    m = monitor; p = parser; rx = rx_drops; dropped = sample_drops; logs = event_drops;
    peak = queue_peak; delay = process_delay_ms; interval = report_ms; now = xTaskGetTickCount();
    taskEXIT_CRITICAL();
    writer_t w = {{0}, 0};
    text(&w, "{\"event\":\"status\""); field(&w, "token", token); field(&w, "tick_ms", now);
    field(&w, "frames", p.frames); field(&w, "crc_errors", p.crc_errors);
    field(&w, "malformed", p.malformed); field(&w, "frame_timeouts", p.timeouts);
    field(&w, "rx_drops", rx); field(&w, "sample_drops", dropped); field(&w, "event_drops", logs);
    field(&w, "queue_peak", peak); field(&w, "range_errors", m.range_errors);
    field(&w, "channel_errors", m.channel_errors); field(&w, "process_delay_ms", delay);
    field(&w, "report_ms", interval); field(&w, "stale_ms", m.stale_ms);
    field(&w, "heap_free", (uint32_t)xPortGetFreeHeapSize());
    text(&w, ",\"stack_free_words\":["); number(&w, (uint32_t)uxTaskGetStackHighWaterMark(parser_handle));
    text(&w, ","); number(&w, (uint32_t)uxTaskGetStackHighWaterMark(monitor_handle));
    text(&w, ","); number(&w, (uint32_t)uxTaskGetStackHighWaterMark(reporter_handle));
    text(&w, "],\"sensors\":[");
    for (unsigned ch=0; ch<CHANNELS; ++ch) {
        sensor_t *s = &m.sensor[ch];
        if (ch) text(&w, ",");
        text(&w, "{\"channel\":"); number(&w, ch);
        text(&w, ",\"state\":\""); text(&w, state_name(monitor_state(&m,ch,now))); text(&w,"\"");
        text(&w, ",\"value\":"); signed_number(&w, s->value);
        text(&w, ",\"threshold\":"); signed_number(&w, m.threshold[ch]);
        field(&w, "accepted", s->accepted); field(&w,"sequence",s->sequence);
        field(&w, "duplicates", s->duplicates); field(&w,"out_of_order",s->out_of_order);
        field(&w, "missing", s->missing); text(&w, "}");
    }
    text(&w, "]}\n"); uart_write(w.data);
}

static void reporter_task(void *arg) {
    (void)arg;
    uart_write("{\"event\":\"boot\",\"platform\":\"mps2-an385\",\"kernel\":\"FreeRTOS-11.1.0\"}\n");
    uint32_t last = xTaskGetTickCount();
    for (;;) {
        event_t e;
        if (xQueueReceive(event_queue, &e, pdMS_TO_TICKS(20)) == pdPASS) {
            if (e.kind == EVENT_STATUS) status(e.token);
            else {
                writer_t w = {{0},0};
                if (e.kind == EVENT_ACK) {
                    text(&w,"{\"event\":\"ack\""); field(&w,"token",e.token); field(&w,"command",e.command);
                    field(&w,"ok",(uint32_t)e.value);
                } else {
                    text(&w,"{\"event\":\"state\""); field(&w,"channel",e.channel);
                    text(&w,",\"state\":\""); text(&w,state_name((unsigned)e.value)); text(&w,"\"");
                }
                text(&w,"}\n"); uart_write(w.data);
            }
        }
        taskENTER_CRITICAL(); uint32_t interval = report_ms; taskEXIT_CRITICAL();
        uint32_t now = xTaskGetTickCount();
        if ((uint32_t)(now-last) >= interval) { last=now; status(0); }
    }
}

int main(void) {
    uart_init(); monitor_init(&monitor);
    rx_queue = xQueueCreate(RX_CAPACITY, sizeof(uint8_t));
    sample_queue = xQueueCreate(SAMPLE_CAPACITY, sizeof(sample_t));
    event_queue = xQueueCreate(EVENT_CAPACITY, sizeof(event_t));
    configASSERT(rx_queue && sample_queue && event_queue);
    configASSERT(xTaskCreate(parser_task,"parser",512,0,3,&parser_handle)==pdPASS);
    configASSERT(xTaskCreate(monitor_task,"monitor",384,0,2,&monitor_handle)==pdPASS);
    configASSERT(xTaskCreate(reporter_task,"reporter",1024,0,1,&reporter_handle)==pdPASS);
    vTaskStartScheduler(); fatal("scheduler_returned");
}
