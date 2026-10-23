#include "monitor.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static unsigned passed;
#define RUN(fn) do { fn(); ++passed; puts("PASS " #fn); } while (0)

static bool deliver(parser_t *p, const uint8_t *wire, size_t n, uint32_t now, frame_t *f) {
    bool complete = false;
    for (size_t i=0;i<n;++i) if (parser_feed(p,wire[i],now,f)) complete=true;
    return complete;
}
static void crc_reference(void) {
    assert(crc16((const uint8_t *)"123456789",9)==0x29b1u);
    assert(crc16((const uint8_t *)"",0)==0xffffu);
}
static void roundtrip_signed_and_escaping(void) {
    const int32_t values[]={INT32_MIN,-4000,-1,0,0x7d,0x7e,INT32_MAX};
    for (size_t i=0;i<sizeof values/sizeof values[0];++i) {
        frame_t input={TYPE_SAMPLE,1,0x7e7d,values[i]},output={0};
        uint8_t wire[24]; size_t n=frame_encode(&input,wire,sizeof wire);
        parser_t p={0}; assert(n && deliver(&p,wire,n,0,&output));
        assert(output.value==input.value && output.sequence==input.sequence);
        assert(output.channel==1 && output.type==TYPE_SAMPLE && p.frames==1);
    }
}
static void insufficient_capacity_is_nonmutating(void) {
    uint8_t out[24]; memset(out,0x5a,sizeof out);
    frame_t f={TYPE_SAMPLE,0,1,42};
    assert(frame_encode(&f,out,2)==0);
    for (size_t i=0;i<sizeof out;++i) assert(out[i]==0x5a);
}
static void crc_corruption_and_resync(void) {
    frame_t f={TYPE_SAMPLE,0,1,2000},out={0}; uint8_t wire[24];
    size_t n=frame_encode(&f,wire,sizeof wire);

    wire[3]^=1u; parser_t p={0};
    assert(!deliver(&p,wire,n,0,&out)); assert(p.crc_errors==1);
    n=frame_encode(&f,wire,sizeof wire);
    assert(deliver(&p,wire,n,1,&out) && p.frames==1);
}
static void oversize_and_short_frame(void) {
    parser_t p={0};frame_t out={0};
    parser_feed(&p,0x7e,0,&out);
    for (unsigned i=0;i<100;++i) parser_feed(&p,0x55,0,&out);
    assert(p.length<=FRAME_BYTES && p.malformed==1);
    assert(!parser_feed(&p,0x7e,0,&out));
    parser_feed(&p,1,1,&out);parser_feed(&p,0x7e,1,&out);
    assert(p.malformed==2);
}
static void escape_delimiter_and_timeout(void) {
    parser_t p={0};frame_t f={0};
    parser_feed(&p,0x7e,UINT32_MAX-10u,&f);
    parser_feed(&p,0x7d,UINT32_MAX-10u,&f);
    parser_expire(&p,88);assert(p.timeouts==0);
    parser_expire(&p,89);assert(p.timeouts==1 && !p.active);
    parser_feed(&p,0x7e,90,&f);parser_feed(&p,0x7d,90,&f);
    parser_feed(&p,0x7e,90,&f);assert(p.malformed==1);
}
static void unsupported_version_and_type(void) {
    for (unsigned kind=0;kind<2;++kind) {
        parser_t p={0};frame_t f={0};
        uint8_t body[11]={1,TYPE_SAMPLE,0,0,1,0,0,0,0,0,0};
        body[kind]=99;
        uint16_t c=crc16(body,9);body[9]=(uint8_t)(c>>8);body[10]=(uint8_t)c;
        parser_feed(&p,0x7e,0,&f);
        for (unsigned i=0;i<11;++i) {
            if (body[i]==0x7e || body[i]==0x7d) {
                parser_feed(&p,0x7d,0,&f);parser_feed(&p,body[i]^0x20u,0,&f);
            } else parser_feed(&p,body[i],0,&f);
        }
        assert(!parser_feed(&p,0x7e,0,&f));assert(p.malformed==1 && p.crc_errors==0);
    }
}
static void monitor_hysteresis_and_staleness(void) {
    monitor_t m;monitor_init(&m);frame_t f={TYPE_SAMPLE,0,1,3000};
    assert(monitor_state(&m,0,0)==SENSOR_UNAVAILABLE);
    assert(monitor_sample(&m,&f,100));assert(monitor_state(&m,0,100)==SENSOR_WARNING);
    f.sequence=2;f.value=2950;assert(monitor_sample(&m,&f,101));assert(m.sensor[0].warning);
    f.sequence=3;f.value=2900;assert(monitor_sample(&m,&f,102));assert(!m.sensor[0].warning);
    assert(monitor_state(&m,0,601)==SENSOR_NORMAL);
    assert(monitor_state(&m,0,602)==SENSOR_UNAVAILABLE);
}
static void sequence_rules_and_wrap(void) {
    monitor_t m;monitor_init(&m);frame_t f={TYPE_SAMPLE,0,65534,2000};
    assert(monitor_sample(&m,&f,0));
    f.sequence=65535;assert(monitor_sample(&m,&f,1));
    f.sequence=0;assert(monitor_sample(&m,&f,2));
    assert(!monitor_sample(&m,&f,3) && m.sensor[0].duplicates==1);
    f.sequence=3;assert(monitor_sample(&m,&f,4));assert(m.sensor[0].missing==2);
    f.sequence=2;assert(!monitor_sample(&m,&f,5));assert(m.sensor[0].out_of_order==1);
    f.sequence=(uint16_t)(3u+0x8000u);assert(!monitor_sample(&m,&f,6));
    assert(m.sensor[0].last_ms==4);
}
static void ranges_and_no_false_refresh(void) {
    monitor_t m;monitor_init(&m);frame_t f={TYPE_SAMPLE,0,1,-4000};
    assert(monitor_sample(&m,&f,UINT32_MAX-20u));
    assert(monitor_state(&m,0,0)==SENSOR_NORMAL);
    f.sequence=2;f.value=-4001;assert(!monitor_sample(&m,&f,0));
    f.value=12501;assert(!monitor_sample(&m,&f,0));
    f.channel=1;f.value=-1;assert(!monitor_sample(&m,&f,0));
    f.value=30000;assert(monitor_sample(&m,&f,0));
    f.channel=2;assert(!monitor_sample(&m,&f,0));
    assert(m.range_errors==3 && m.channel_errors==1);
    assert(m.sensor[0].last_ms==UINT32_MAX-20u);
}
static void parser_noise_stress(void) {
    parser_t p={0};frame_t f={0};uint32_t random=42;
    for (uint32_t i=0;i<100000;++i) {
        random=random*1664525u+1013904223u;
        (void)parser_feed(&p,(uint8_t)(random>>24),i,&f);
        assert(p.length<=FRAME_BYTES);
    }
    uint8_t wire[24];frame_t known={TYPE_SAMPLE,0,17,2500};
    size_t n=frame_encode(&known,wire,sizeof wire);
    parser_expire(&p,100100);
    assert(deliver(&p,wire,n,100101,&f) && f.value==2500 && f.sequence==17);
}
int main(void) {
    RUN(crc_reference);RUN(roundtrip_signed_and_escaping);
    RUN(insufficient_capacity_is_nonmutating);RUN(crc_corruption_and_resync);
    RUN(oversize_and_short_frame);RUN(escape_delimiter_and_timeout);
    RUN(unsupported_version_and_type);RUN(monitor_hysteresis_and_staleness);
    RUN(sequence_rules_and_wrap);RUN(ranges_and_no_false_refresh);RUN(parser_noise_stress);
    printf("%u C test groups passed (including 100,000-byte noise stress).\n",passed);
    return 0;
}
