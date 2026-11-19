/* First-use device conversion; no game CPU runs here. GPL-2.0-or-later. */
#ifndef H1_IMPORT_GAME_H
#define H1_IMPORT_GAME_H
/* Return nonzero to continue. Stages: inspect, program, sound, fixed, sprites, verify, done. */
typedef int (*h1_import_progress)(void *,const char *,unsigned,unsigned);
/* Native paths are GBK bytes. Output includes room for the temporary suffix. */
int h1_import_paths(const char *zip,char output[240],char bios[240]);
void h1_import_find_bios(char path[240],int pgm);
/* 1 converted; 2 reused; 0 canceled; -1 error. Never replaces output. */
int h1_import_game(const char *zip,const char *bios,const char *output,
                   h1_import_progress progress,void *user,char error[128]);
#endif
