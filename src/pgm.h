/* Experimental PGM / Knights of Valour adapter. GPL-2.0-or-later. */
#ifndef H1_PGM_H
#define H1_PGM_H
#include <stdint.h>
#include "import_game.h"
extern unsigned h1_pgm_active;
extern uint8_t **h1_pgm_bus_pages;
extern volatile struct h1_pgm_diagnostics {
 unsigned magic,version,state,error,frames,draws,pc,keys,hits,misses,protection,last_command;
 unsigned bios_tests,game_started,unknown_reads,unknown_writes;
} h1_pgm_stats;
int h1_pgm_paths(const char *,char[240],char[240],char[16]);
int h1_pgm_cached(const char *,const char *);
int h1_pgm_import(const char *,const char *,const char *,const char *,h1_import_progress,void *,char[128]);
int h1_pgm_load(const char *);
void h1_pgm_close(void);
uint8_t *h1_pgm_code(unsigned);
unsigned h1_pgm_read8(unsigned);
unsigned h1_pgm_read16(unsigned);
void h1_pgm_write8(unsigned,unsigned);
void h1_pgm_write16(unsigned,unsigned);
void h1_pgm_inputs(unsigned);
void h1_pgm_frame(unsigned);
void h1_pgm_present(uint32_t *,unsigned);
void h1_pgm_pause_probe(void);
unsigned h1_pgm_prot_read(unsigned);
void h1_pgm_prot_write(unsigned,unsigned);
void h1_pgm_prot_reset(void);
void h1_pgm_decrypt(uint8_t *,unsigned);
#endif
