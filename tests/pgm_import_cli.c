/* Host test of the file converter only; never executes a game CPU. */
#include "pgm.h"
#include <stdio.h>
#include <stdlib.h>
static int progress(void *p,const char *stage,unsigned done,unsigned total){(void)stage;(void)total;return !p||done<strtoul(p,0,10);}
int main(int argc,char **argv){char error[128];if(argc<4)return 2;int r=h1_pgm_import(argv[1],argv[2],argv[3],"kov",progress,argc>4?argv[4]:0,error);printf("%d %s\n",r,error);return r==1?0:r==0?3:1;}
