/* PGM ICS2115 binding; upstream synthesis retains its original terms. */
#ifndef H1_PGM_SOUND_H
#define H1_PGM_SOUND_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
extern int nBurnSoundLen;
extern int16_t *pBurnSoundOut;
void z80_set_irq_line(int,int);
#define ZET_IRQSTATUS_ACK 1
#define ZET_IRQSTATUS_NONE 0
#define ZetSetIRQLine(vector,state) z80_set_irq_line(0,state)
unsigned char h1_pgm_sample(unsigned);
int ics2115_init(void);
void ics2115_exit(void);
void ics2115_reset(void);
void ics2115_frame(void);
void ics2115_update(int);
unsigned char ics2115read(unsigned char);
void ics2115write(unsigned char,unsigned char);
unsigned short ics2115_soundlatch_r(int);
void ics2115_soundlatch_w(int,unsigned short);
unsigned char h1_pgm_z80_read(unsigned short);
void h1_pgm_z80_write(unsigned short,unsigned char);
#endif
