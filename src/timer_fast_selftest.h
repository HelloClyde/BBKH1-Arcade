/* Actual scheduler arithmetic, compared with the original three-slot loop.
 * No guest CPU is run by this fixture. Deadlines and clock bits must match. */
#include <string.h>
#include "h1_port.h"
static unsigned h1_timer_test_hash,h1_timer_test_events;
static void h1_timer_test_callback(int param){
 h1_timer_test_hash=h1_timer_test_hash*1664525u+(unsigned)param;
 ++h1_timer_test_events;
 if((param&7)==3)insert_timer(0.000448,param+1,h1_timer_test_callback);
}
int h1_timer_selftest(void){
 timer_struct saved[3],reference[3];unsigned saved_remaining[3];
 memcpy(saved,timers,sizeof saved);memcpy(saved_remaining,h1_timer_remaining,sizeof saved_remaining);
 double saved_count=timer_count,saved_inc=inc;int saved_init=h1_timer_init;
 static const double durations[]={-0.00001,0,0.000063999999999,0.000064,
  0.000064000000001,0.00032,0.000448,0.000999,0.003072,0.1,0.999,1,2};
 for(unsigned test=0;test<16;test++){
  double reference_clock=0;unsigned reference_hash=0,reference_events=0;
  for(unsigned pass=0;pass<2;pass++){
   memset(timers,0,sizeof timers);memset(h1_timer_remaining,0,sizeof h1_timer_remaining);free_all_timer();
   timer_count=(double[]){0,1000,65535,65537}[test&3];inc=0.016896/264;
   h1_timer_init=0;h1_timer_fast=1;unsigned rng=4740+test;
   h1_timer_test_hash=h1_timer_test_events=0;
   for(unsigned step=0;step<2048;step++){
    rng=rng*1664525u+1013904223u;unsigned slot=(rng>>12)%3;
    if(!(rng&7)){
     if(!timers[slot].del_it)del_timer(&timers[slot]);
     insert_timer(durations[(rng>>16)%13],rng>>24,h1_timer_test_callback);
    }
    if(!pass){
     timer_count+=inc;
     for(unsigned i=0;i<3;i++)if(!timers[i].del_it&&timer_count>=timers[i].time){
      if(timers[i].func)timers[i].func(timers[i].param);
      timers[i].del_it=1;
     }
    }else my_timer();
   }
   if(!pass){
    memcpy(reference,timers,sizeof reference);reference_clock=timer_count;
    reference_hash=h1_timer_test_hash;reference_events=h1_timer_test_events;
   }else{
    ++h1_timer_fast_stats.tests;
    if(memcmp(reference,timers,sizeof reference)||memcmp(&reference_clock,&timer_count,sizeof(double))||
       reference_hash!=h1_timer_test_hash||reference_events!=h1_timer_test_events){
     ++h1_timer_fast_stats.failures;h1_arcade_log("TIMER_FAST_FAIL case=%u",test);return 0;
    }
   }
  }
 }
 memcpy(timers,saved,sizeof saved);memcpy(h1_timer_remaining,saved_remaining,sizeof saved_remaining);
 timer_count=saved_count;inc=saved_inc;h1_timer_init=saved_init;h1_timer_fast=1;
 h1_arcade_log("TIMER_FAST_OK cases=%u exact_clock_deadlines_and_callbacks=1",h1_timer_fast_stats.tests);
 return 1;
}
