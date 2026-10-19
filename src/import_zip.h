/* Bounded, streaming ZIP reader. GPL-2.0-or-later. */
#ifndef H1_IMPORT_ZIP_H
#define H1_IMPORT_ZIP_H
#include <stdint.h>
typedef struct h1_zip h1_zip;
typedef struct h1_zip_stream h1_zip_stream;
h1_zip *h1_zip_open(const char *path);
void h1_zip_close(h1_zip *zip);
int h1_zip_find(h1_zip *,const char *name,uint32_t crc,int match_crc);
unsigned h1_zip_size(h1_zip *,int member);
h1_zip_stream *h1_zip_start(h1_zip *,int member);
int h1_zip_read(h1_zip_stream *,void *out,unsigned bytes);
int h1_zip_finish(h1_zip_stream *);
void h1_zip_stop(h1_zip_stream *);
uint32_t h1_import_crc(uint32_t state,const void *data,unsigned bytes);
#endif
