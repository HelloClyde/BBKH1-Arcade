/* Inject filesystem failures into the actual importer, not a converter model. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static FILE *writing;
static unsigned write_count;
static int injected(const char *kind){const char *p=getenv("IMPORT_IO_FAULT");return p&&!strcmp(p,kind);}
static FILE *test_open(const char *path,const char *mode){FILE *f=fopen(path,mode);if(f&&!strcmp(mode,"wb"))writing=f;return f;}
static size_t test_write(const void *p,size_t size,size_t n,FILE *f){
 if(f==writing&&++write_count==4&&injected("write"))return 0;
 return fwrite(p,size,n,f);
}
static int test_close(FILE *f){int fail=f==writing&&injected("close");if(f==writing)writing=0;int result=fclose(f);return fail?EOF:result;}
static int test_rename(const char *from,const char *to){return injected("rename")?-1:rename(from,to);}
#define fopen test_open
#define fwrite test_write
#define fclose test_close
#define rename test_rename
#include "../src/import_game.c"
