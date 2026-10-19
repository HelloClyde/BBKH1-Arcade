/* First-use device conversion; no game CPU runs here. GPL-2.0-or-later. */
#ifndef H1_IMPORT_GAME_H
#define H1_IMPORT_GAME_H
/* Return nonzero to continue. Stages: inspect, program, sound, fixed, sprites, verify, done. */
typedef int (*h1_import_progress)(void *,const char *,unsigned,unsigned);
/* 1 success; 0 user cancellation; -1 error (message provided). Never replaces output. */
int h1_import_game(const char *zip,const char *bios,const char *output,const char *set,
                   h1_import_progress progress,void *user,char error[128]);
int h1_import_cached(const char *path,const char *set);
#endif
