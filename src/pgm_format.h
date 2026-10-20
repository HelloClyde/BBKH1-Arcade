/* H1PGM1: little-endian metadata, raw ROM sections, page CRCs. */
#ifndef H1_PGM_FORMAT_H
#define H1_PGM_FORMAT_H
#include <stdint.h>
enum {PGM_BIOS,PGM_PROGRAM,PGM_TILES,PGM_COLOR,PGM_MASK,PGM_SOUND,PGM_REGIONS};
#define PGM_PAGE 4096u
#define PGM_HEADER 256u
typedef struct {uint32_t offset,size,crc,pages;} pgm_part;
static inline uint32_t pgm_u32(const uint8_t *p){return p[0]|p[1]<<8|p[2]<<16|(uint32_t)p[3]<<24;}
static inline void pgm_put32(uint8_t *p,uint32_t v){p[0]=v;p[1]=v>>8;p[2]=v>>16;p[3]=v>>24;}
int h1_pgm_header(const uint8_t *,unsigned,const char *);
#endif
