#include "h1_sdk.h"
#include "audio.h"
#include "diagnostics.h"
#include "profile.h"
#include <string.h>
#include "spu_profile.h"
#include <stdlib.h>
#if H1_AUDIO_FAST
#include "audio_fast.h"
#endif

#define OUTPUT_RATE 32000u
#define BLOCK_SAMPLES 8192u
#define PREFILL 4096u
#define SLOTS 1u
typedef struct {
    int16_t *pcm;
    uint32_t bytes;
    uint32_t private_words[6];
} pcm_descriptor;
typedef int (*descriptor_fn)(pcm_descriptor *);
typedef int (*device_fn)(void);
typedef int (*open_fn)(uint32_t *);
typedef int (*submit_fn)(int, pcm_descriptor *, int, int);
typedef void (*volume_set_fn)(int);
static volume_set_fn set_volume;
static device_fn get_volume;
static unsigned volume, volume_requested, original_volume, volume_owned;
static pcm_descriptor descriptors[SLOTS] __attribute__((aligned(32)));
static int16_t samples[SLOTS][BLOCK_SAMPLES] __attribute__((aligned(32)));
static descriptor_fn initialize, destroy;
static device_fn start, stop, close_device;
static submit_fn submit;
static volatile uint32_t *queue;
static unsigned initialized, active, rate, phase, used, read_position, queued, playing;
static int32_t accumulator;
static unsigned batches, submitted, dropped, failures, nonzero, underruns;
static unsigned waiting, progress_tick, progress_position;
static unsigned paused;
/* Exact PCM A/B switch; the generic producer still owns all edge cases. */
volatile unsigned h1_audio_batch_mode=1;
volatile struct {unsigned magic,version,calls,outputs;} h1_audio_batch_stats={0x31424350,1};
#if H1_PROFILE
extern uint32_t h1_wall_clock(void);
typedef struct {
    unsigned batch,raw_tick,rtc,queued,consumed,old_position,new_position,reset_ticks;
} audio_gap;
static audio_gap gaps[16];
static unsigned gap_count,gap_total;
static void record_gap(unsigned consumed,unsigned position)
{
    audio_gap *p=&gaps[gap_count%16];
    p->batch=batches;p->raw_tick=h1_raw_tick_80hz();p->rtc=h1_wall_clock();
    p->queued=queued;p->consumed=consumed;p->old_position=read_position;p->new_position=position;
    p->reset_ticks=0;
    ++gap_count;++gap_total;
}
static void finish_gap(void)
{
    audio_gap *p=&gaps[(gap_count-1)%16];
    p->reset_ticks=h1_raw_tick_80hz()-p->raw_tick;
}
static void dump_gaps(const char *reason)
{
    unsigned start=gap_count>16 ? gap_count-16 : 0;
    h1_diag_batch_begin();
    h1_diag("AUDIO_GAPS reason=%s count=%u omitted=%u total=%u",reason,gap_count-start,start,gap_total);
    for (unsigned i=start;i<gap_count;++i) {
        audio_gap *p=&gaps[i%16];
        h1_diag("AUDIO_GAP batch=%u rtc=%u raw_tick=%u queued=%u consumed=%u missing=%u old_position=%u new_position=%u reset_ticks=%u",p->batch,p->rtc,p->raw_tick,p->queued,p->consumed,p->consumed-p->queued,p->old_position,p->new_position,p->reset_ticks);
    }
    gap_count=0;
    h1_diag_batch_end();
}
#else
static void record_gap(unsigned consumed,unsigned position) { (void)consumed;(void)position; }
static void finish_gap(void) {}
static void dump_gaps(const char *reason) { (void)reason; }
#endif

static void *service(unsigned offset)
{
    return h1_runtime_entry(h1_runtime_table(H1_RUNTIME_SYS_TABLE_SLOT), offset);
}

static void resolve_volume(void)
{
    const uint32_t *setter=service(0x40u), *getter=service(0x44u);
    /* V1.41 setter clamps to 100; getter loads a global in its return slot.
     * Reject unknown implementations rather than calling an unverified ABI. */
    set_volume=0;get_volume=0;
    if (!setter || !getter || setter[0]!=0x28850065u || setter[1]!=0x24030064u ||
        (getter[0]&0xffff0000u)!=0x3c020000u || getter[1]!=0x03e00008u ||
        (getter[2]&0xffff0000u)!=0x8c420000u) return;
    set_volume=(volume_set_fn)setter;get_volume=(device_fn)getter;
}
void gba_audio_set_volume(unsigned percent)
{
    volume=percent>100u?100u:percent;
    volume_requested=1;
    if (active && volume_owned) {
        set_volume((int)volume);
        h1_diag("AUDIO_VOLUME percent=%u native=1",volume);
    }
}

/* V1.41 has no public completion callback. Resolve the queue's address from
 * the supplied submit implementation, accepting only its verified instruction
 * sequence. Queue nodes contain next, descriptor, cursor, end, repeats, flags.
 * This is read-only; never assume a fixed firmware RAM address. */
static volatile uint32_t *resolve_queue(submit_fn function)
{
    const uint32_t *code = (const uint32_t *)function;
    uint32_t address;
    if (!code || (code[0x70 / 4] & 0xffff0000u) != 0x3c060000u ||
        (code[0x74 / 4] & 0xffff0000u) != 0x24c60000u ||
        code[0x78 / 4] != 0x00c43021u)
        return 0;
    address = ((code[0x70 / 4] & 65535u) << 16) + (int16_t)code[0x74 / 4];
    if ((address & 3u) || address < 0x80000000u || address >= 0x84000000u) return 0;
    return (volatile uint32_t *)address;
}
static void reset_ring(void)
{
    H1_SPU_BEGIN(tick,HSP_RESET,1);
    /* Cancellation is serialized with the firmware mixer. Only then clear
     * samples or restart the ring, so no active source can be overwritten. */
    H1_SPU_BEGIN(submit_tick,HSP_SUBMIT,1);
    submit(0, 0, 0, 0);
    H1_SPU_END(HSP_SUBMIT,submit_tick);
    playing = queued = used = read_position = 0;
    memset(samples, 0, sizeof(samples));
    H1_SPU_END(HSP_RESET,tick);
}
static void retire_inner(void)
{
    volatile uint32_t *node;
    uint32_t address, cursor, base = (uint32_t)samples[0];
    unsigned position, consumed, i;
    if (!playing) return;
    node = (volatile uint32_t *)queue[0]; address = (uint32_t)node;
    if (!node || (address & 3u) || address < 0x80000000u || address >= 0x84000000u ||
        node[1] != (uint32_t)&descriptors[0]) { ++failures; reset_ring(); return; }
    cursor = node[2];
    if (cursor < base || cursor > base + sizeof(samples) || (cursor & 1u)) {
        ++failures; reset_ring(); return;
    }
    position = ((cursor - base) / 2) % BLOCK_SAMPLES;
    consumed = (position + BLOCK_SAMPLES - read_position) % BLOCK_SAMPLES;
    if (consumed > queued) { record_gap(consumed,position);++underruns; reset_ring(); finish_gap();return; }
    /* Erase retired data; a slow producer then yields silence, not old audio.
     * Unread samples remain owned by the mixer until cursor passes them. */
    for (i = 0; i < consumed; ++i) samples[0][(read_position + i) % BLOCK_SAMPLES] = 0;
    read_position = position; queued -= consumed;
}
static void retire(void)
{
    H1_SPU_BEGIN(tick,HSP_RETIRE,1);
    retire_inner();
    H1_SPU_END(HSP_RETIRE,tick);
}
int gba_audio_open(unsigned source_rate)
{
    uint32_t config[9] = {OUTPUT_RATE, 1, 4096, 0, 0, 0, 0, 0, 0};
    open_fn open_device = (open_fn)service(0x58u);
    int result, previous=-1;
    unsigned i;
    gba_audio_close();
    resolve_volume();
    if (get_volume) previous=get_volume();
    initialize = (descriptor_fn)service(0x50u); destroy = (descriptor_fn)service(0x54u);
    submit = (submit_fn)service(0x5cu); start = (device_fn)service(0x60u);
    stop = (device_fn)service(0x64u); close_device = (device_fn)service(0x68u);
    queue = resolve_queue(submit);
    h1_diag("AUDIO_OPEN_BEGIN source=%u output=%u mono=1 queue=%p", source_rate, OUTPUT_RATE, (void *)queue);
    if (!source_rate || !queue || !initialize || !destroy || !open_device || !start || !stop || !close_device) {
        h1_diag("AUDIO_OPEN_END ready=0 reason=unsupported_services"); return 0;
    }
    memset(descriptors, 0, sizeof(descriptors)); memset(samples, 0, sizeof(samples));
    initialized = batches = submitted = dropped = failures = nonzero = underruns = 0;
    h1_audio_batch_stats.calls=h1_audio_batch_stats.outputs=0;
#if H1_PROFILE
    gap_count=gap_total=0;
#endif
    rate = source_rate; phase = used = read_position = queued = playing = 0; accumulator = 0;
    waiting = paused = 0;
    for (i = 0; i < SLOTS; ++i) {
        descriptors[i].pcm = samples[i]; descriptors[i].bytes = BLOCK_SAMPLES * 2;
        h1_diag("AUDIO_DESCRIPTOR_BEGIN slot=%u", i);
        result = initialize(&descriptors[i]);
        h1_diag("AUDIO_DESCRIPTOR_END slot=%u rc=%d", i, result);
        if (!result) { gba_audio_close(); return 0; }
        ++initialized;
    }
    h1_diag("AUDIO_DEVICE_BEGIN"); result = open_device(config);
    h1_diag("AUDIO_DEVICE_END rc=%d", result);
    if (!result) { gba_audio_close(); return 0; }
    active = 1;
    if (set_volume && get_volume) {
        if (previous>=0 && previous<=100) {
            original_volume=(unsigned)previous;volume_owned=1;
            if(volume_requested)set_volume((int)volume);
        }
    }
    if (!volume_owned) h1_diag("AUDIO_VOLUME unavailable=1");
    h1_diag("AUDIO_START_BEGIN"); result = start(); h1_diag("AUDIO_START_END rc=%d", result);
    if (!result) { gba_audio_close(); return 0; }
    h1_diag("AUDIO_OPEN_END ready=1"); return 1;
}
static void output(int16_t sample)
{
    if (queued == BLOCK_SAMPLES) { ++dropped; return; }
    samples[0][used] = sample;
    used = (used + 1) % BLOCK_SAMPLES; ++queued;
    if (sample) ++nonzero;
    if (!playing && queued >= PREFILL) {
        int result, trace = submitted < 4 && h1_diag_is_verbose();
        if (trace) h1_diag("AUDIO_SUBMIT_BEGIN ring_bytes=%u prefill=%u", BLOCK_SAMPLES * 2u, PREFILL);
        /* One persistent ring avoids V1.41's short-block queue counter growth.
         * repeats=0 loops; cursor retirement above controls buffer ownership. */
        H1_SPU_BEGIN(tick,HSP_SUBMIT,1);
        result = submit(0, &descriptors[0], 0, 0);
        H1_SPU_END(HSP_SUBMIT,tick);
        if (trace) h1_diag("AUDIO_SUBMIT_END rc=%d", result);
        if (!result) { ++failures; queued = used = 0; }
        else playing = 1;
        ++submitted;
    }
}
#if H1_AUDIO_FAST
static int output_batch(const int16_t *stereo,size_t frames,unsigned copies)
{
    /* Startup/submission, partial capacity and odd ring positions retain
     * output()'s exact ordering and failure/drop handling. With playback
     * already active, only the app writes these queue bookkeeping fields. */
    if (!playing || (used & 1u) || queued > BLOCK_SAMPLES ||
        frames > (BLOCK_SAMPLES - queued) / copies) return 0;
    uint32_t *ring=(uint32_t *)samples[0];
    unsigned position=used/2u,nz=0;
    if(copies==4){
        for(size_t i=0;i<frames;i++){
            int16_t mono=((int32_t)stereo[i*2]+stereo[i*2+1])/2;
            uint32_t pair=(uint16_t)mono|((uint32_t)(uint16_t)mono<<16);
            ring[position]=pair;ring[(position+1u)&(BLOCK_SAMPLES/2u-1u)]=pair;
            position=(position+2u)&(BLOCK_SAMPLES/2u-1u);if(mono)nz+=4;
        }
    }else{
        for(size_t i=0;i<frames;i++){
            int16_t mono=((int32_t)stereo[i*2]+stereo[i*2+1])/2;
            uint32_t pair=(uint16_t)mono|((uint32_t)(uint16_t)mono<<16);
            ring[position]=pair;position=(position+1u)&(BLOCK_SAMPLES/2u-1u);if(mono)nz+=2;
        }
    }
    unsigned produced=(unsigned)frames*copies;
    used=position*2u;queued+=produced;nonzero+=nz;
    ++h1_audio_batch_stats.calls;h1_audio_batch_stats.outputs+=produced;return 1;
}
static void output_repeated(const int16_t *stereo,size_t frames,unsigned copies)
{
    if(h1_audio_batch_mode&&output_batch(stereo,frames,copies))return;
    for(size_t i=0;i<frames;i++){
        int16_t mono=((int32_t)stereo[i*2]+stereo[i*2+1])/2;
        output(mono);output(mono);if(copies==4){output(mono);output(mono);}
    }
}
#endif
size_t gba_audio_write(const int16_t *stereo, size_t frames)
{
    size_t i;
    if (!active || paused || !stereo) return frames;
    ++batches;
    retire();
    H1_SPU_BEGIN(tick,HSP_RESAMPLE,1);
#if H1_AUDIO_FAST
    if((rate==16000u || rate==8000u) && phase==0){
        /* Exact box-filter result for 16 -> 32 kHz: two copies of each mono
         * input, without fractional weights, multiplication or division. */
        output_repeated(stereo,frames,rate==8000u?4:2);
        H1_SPU_END(HSP_RESAMPLE,tick);
        return frames;
    }
#endif
    /* Weighted box resampling, preserving fractional position across batches.
     * Fold stereo to mono before filtering; accumulation stays within int32. */
    for (i = 0; i < frames; ++i) {
        int32_t sample = ((int32_t)stereo[i * 2] + stereo[i * 2 + 1]) / 2;
        unsigned remaining = OUTPUT_RATE;
        while (remaining) {
            unsigned weight = rate - phase;
            if (weight > remaining) weight = remaining;
            accumulator += sample * (int32_t)weight;
            phase += weight; remaining -= weight;
            if (phase == rate) {
                int32_t average = rate == 32768u ? accumulator / 32768 :
                                  rate == 65536u ? accumulator / 65536 : accumulator / (int32_t)rate;
                output((int16_t)average); phase = 0; accumulator = 0;
            }
        }
    }
    H1_SPU_END(HSP_RESAMPLE,tick);
    return frames;
}
#if H1_AUDIO_FAST
void gba_audio_source_rate(unsigned source_rate){
 if(source_rate!=8000&&source_rate!=16000&&source_rate!=22050)return;
 rate=source_rate;phase=0;accumulator=0;
}
static unsigned batch_test_calls,batch_test_result,batch_test_errors;
static int batch_test_submit(int channel,pcm_descriptor *descriptor,int repeat,int flags){
 ++batch_test_calls;if(channel||descriptor!=&descriptors[0]||repeat||flags)++batch_test_errors;
 return (int)batch_test_result;
}
typedef struct {unsigned used,queued,playing,submitted,dropped,failures,nonzero,batches,phase,accumulator,calls,errors;} batch_test_state;
static batch_test_state batch_test_snapshot(void){
 batch_test_state s={used,queued,playing,submitted,dropped,failures,nonzero,batches,phase,(unsigned)accumulator,batch_test_calls,batch_test_errors};return s;
}
static void batch_test_prepare(unsigned position,unsigned fill,unsigned running,unsigned success){
 memset(samples,0xa5,sizeof samples);used=position;queued=fill;playing=running;
 submitted=4;dropped=7;failures=11;nonzero=13;batches=17;phase=0;accumulator=0;
 batch_test_calls=batch_test_errors=0;batch_test_result=success;
}
static int batch_output_selftest(const int16_t *input){
 static const unsigned positions[]={0,1,2,BLOCK_SAMPLES-2,BLOCK_SAMPLES-1,4096};
 static const unsigned fills[]={0,1,PREFILL-6,PREFILL-1,PREFILL,BLOCK_SAMPLES-2,BLOCK_SAMPLES-1,BLOCK_SAMPLES};
 static const unsigned lengths[]={0,1,2,7,64,133,134,1024};
 uint8_t *expected=malloc(sizeof samples);if(!expected)return 0;
 submit_fn saved_submit=submit;unsigned saved_mode=h1_audio_batch_mode;int ok=1;
 submit=batch_test_submit;
 /* Compare the real producer's full ring and bookkeeping, including odd
  * positions, wrapping, full/partial capacity, prefill and submit failure.
  * The only replacement is the firmware submit service; no mixer runs. */
 for(unsigned fixture=0;fixture<192&&ok;fixture++){
  unsigned position=positions[fixture%6],fill=fills[(fixture/3)%8],n=lengths[(fixture/5)%8],copies=fixture&1?4:2;
  batch_test_state reference;
  for(unsigned fast=0;fast<2;fast++){
   batch_test_prepare(position,fill,(fixture/7)&1,(fixture/11)&1);h1_audio_batch_mode=fast;
   output_repeated(input,n,copies);batch_test_state actual=batch_test_snapshot();
   if(!fast){memcpy(expected,samples,sizeof samples);reference=actual;}
   else if(memcmp(expected,samples,sizeof samples)||memcmp(&reference,&actual,sizeof actual)||actual.errors)ok=0;
  }
  ++h1_audio_fast_stats.tests;if(!ok)++h1_audio_fast_stats.failures;
 }
 /* Also exercise the public resampler at all supported source rates and
  * fractional phases. Starting stopped makes retirement a no-op; crossing
  * prefill still invokes the stub above with the real descriptor ABI. */
 static const unsigned rates[]={8000,16000,22050};
 for(unsigned fixture=0;fixture<36&&ok;fixture++){
  batch_test_state reference;unsigned source_rate=rates[fixture%3];
  for(unsigned fast=0;fast<2;fast++){
   batch_test_prepare(positions[(fixture/3)%6],fixture&1?PREFILL-1:0,0,(fixture/5)&1);
   rate=source_rate;phase=fixture&2?source_rate/3:0;active=1;paused=0;h1_audio_batch_mode=fast;
   gba_audio_write(input,133);active=0;batch_test_state actual=batch_test_snapshot();
   if(!fast){memcpy(expected,samples,sizeof samples);reference=actual;}
   else if(memcmp(expected,samples,sizeof samples)||memcmp(&reference,&actual,sizeof actual)||actual.errors)ok=0;
  }
  ++h1_audio_fast_stats.tests;if(!ok)++h1_audio_fast_stats.failures;
 }
 submit=saved_submit;h1_audio_batch_mode=saved_mode;free(expected);return ok;
}
int gba_audio_resampler_selftest(void){
 if(active||initialized)return 0;
 static int16_t input[2048];unsigned seed=4740;
 for(unsigned test=0;test<128;test++){
  unsigned fixture=test%64,n=1+(fixture*37)%1024;
  for(unsigned i=0;i<n*2;i++){seed=seed*1664525u+1013904223u;input[i]=(int16_t)seed;}
  if(fixture<4)for(unsigned i=0;i<n;i++){input[i*2]=fixture==0?-32768:32767;input[i*2+1]=fixture==2?-32768:input[i*2];}
  unsigned copies=test<64?2:4;
  /* This arithmetic fixture owns no firmware playback service. Never cross
   * the real output() submit threshold while using its private ring. */
  if(n*copies>=PREFILL){++h1_audio_fast_stats.tests;++h1_audio_fast_stats.failures;return 0;}
  memset(samples,0xa5,sizeof samples);active=1;paused=playing=queued=used=phase=accumulator=0;rate=test<64?16000:8000;
  gba_audio_write(input,n);active=0;
  unsigned bad=used!=n*copies||queued!=n*copies||phase||accumulator||playing;
  for(unsigned i=0;i<n&&!bad;i++){int16_t mono=((int32_t)input[i*2]+input[i*2+1])/2;for(unsigned k=0;k<copies;k++)bad|=samples[0][i*copies+k]!=mono;}
  for(unsigned i=n*copies;i<BLOCK_SAMPLES&&!bad;i++)bad=samples[0][i]!=(int16_t)0xa5a5;
  ++h1_audio_fast_stats.tests;if(bad){++h1_audio_fast_stats.failures;return 0;}
 }
 if(!batch_output_selftest(input))return 0;
 h1_audio_batch_stats.calls=h1_audio_batch_stats.outputs=0;
 batches=submitted=dropped=failures=nonzero=underruns=0;queued=used=phase=accumulator=0;playing=active=paused=0;memset(samples,0,sizeof samples);
 return 1;
}
#endif
int gba_audio_pacing(void) { return active && !paused; }
int gba_audio_draw_budget(void)
{
    if (!active || paused || !playing) return 1;
    retire();
    return queued >= PREFILL - 1024u;
}
int gba_audio_wait(void)
{
    unsigned now;
    if (!active || paused) return 0;
    retire();
    if (!playing || queued <= PREFILL) { waiting = 0; return 0; }
    now = h1_raw_tick_80hz();
    if (!waiting || progress_position != read_position) {
        waiting = 1; progress_tick = now; progress_position = read_position;
    } else if ((unsigned)(now - progress_tick) > 80u) {
        h1_diag("AUDIO_STALL disabling_output=1");
        gba_audio_close(); return 0;
    }
    return 1;
}
void gba_audio_pause(void)
{
    if (!active || paused) return;
    stop(); reset_ring();
    phase=0; accumulator=0; waiting=0; paused=1;
    h1_diag("AUDIO_PAUSE batches=%u underruns=%u failures=%u",batches,underruns,failures);
    dump_gaps("pause");
}
int gba_audio_resume(void)
{
    if (!active) return 0;
    if (!paused) return 1;
    /* Keep the initialized device and descriptor. Logging and storage work
     * must finish before restarting DMA, otherwise the empty ring can starve. */
    if (!start()) { gba_audio_close(); return 0; }
    paused=0; waiting=0;
    return 1;
}
void gba_audio_close(void)
{
    unsigned i;
    if (active) {
        h1_diag("AUDIO_CLOSE_BEGIN batches=%u submitted=%u nonzero=%u dropped=%u failures=%u underruns=%u", batches, submitted, nonzero, dropped, failures, underruns);
        stop(); submit(0, 0, 0, 0); close_device(); active = 0;
        if (volume_owned) {
            set_volume((int)original_volume);volume_owned=0;
            h1_diag("AUDIO_VOLUME_RESTORE percent=%u",original_volume);
        }
        dump_gaps("close");
    }
    for (i = 0; i < initialized; ++i) destroy(&descriptors[i]);
    if (initialized) h1_diag("AUDIO_CLOSE_END descriptors=%u", initialized);
    initialized = 0; queue = 0; used = playing = queued = paused = 0;
}
