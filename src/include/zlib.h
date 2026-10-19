/* Legacy state/cache declarations only. H1 uses raw pages and CRC32;
 * no zlib implementation is linked into this build. */
#ifndef H1_ZLIB_DECLARATIONS_H
#define H1_ZLIB_DECLARATIONS_H
typedef void *gzFile;
typedef unsigned long uLong;
typedef unsigned long uLongf;
uLong compressBound(uLong);
int uncompress(unsigned char *,uLongf *,const unsigned char *,uLong);
#endif
