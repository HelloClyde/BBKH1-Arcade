#ifndef H1_SETJMP_H
#define H1_SETJMP_H
#include "h1_port.h"
typedef int jmp_buf[12];
#define setjmp(b) h1_arcade_setjmp(b)
#define longjmp(b,v) h1_arcade_longjmp(b)
#endif
