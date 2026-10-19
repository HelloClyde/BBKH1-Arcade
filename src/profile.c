#include "h1_sdk.h"
#include "profile.h"
#if H1_PROFILE
/* Tick boundaries partition elapsed time. Short sections need a long window;
 * these are coarse samples, not precise per-call timings or CPU cycle counts. */
volatile struct {
 unsigned magic,version,running,phase,last,calls,ticks[HP_COUNT];
} h1_arcade_profile={0x31465250,2};
unsigned h1_profile_set(unsigned phase){
 unsigned previous=h1_arcade_profile.phase;
 if(h1_arcade_profile.running){
  unsigned now=h1_raw_tick_80hz();
  h1_arcade_profile.ticks[previous]+=now-h1_arcade_profile.last;
  h1_arcade_profile.last=now;h1_arcade_profile.phase=phase;
  ++h1_arcade_profile.calls;
 }
 return previous;
}
void h1_profile_start(void){h1_arcade_profile.last=h1_raw_tick_80hz();h1_arcade_profile.running=1;}
void h1_profile_stop(void){h1_profile_set(HP_OTHER);h1_arcade_profile.running=0;}
#endif
