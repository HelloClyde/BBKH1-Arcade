/* Run the actual import C functions on a host, without any game CPU. */
#include "import_game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int notify(void *user,const char *stage,unsigned done,unsigned total){
 (void)done;(void)total;return !user||strcmp(stage,(const char *)user);
}
int main(int argc,char **argv){
 char error[128];if(argc<5)return 2;
 int result=h1_import_game(argv[1],argv[2],argv[3],argv[4],notify,argc>5?argv[5]:0,error);
 printf("result=%d error=%s\n",result,error);return result==1?0:result==0?3:1;
}
