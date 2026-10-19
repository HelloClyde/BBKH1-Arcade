#ifndef H1_ARCADE_MENU_H
#define H1_ARCADE_MENU_H
#include "arcade_config.h"
typedef struct {unsigned magic,version,active,page,cursor,capture,opened,closed,action,touches,edits,save_ok;} arcade_menu_stats;
extern volatile arcade_menu_stats h1_menu_stats;
/* Caller freezes guest CPU and pauses native PCM. 0 resume, 2 exit. */
int arcade_pause_menu(arcade_settings *,uint32_t *(*target)(void),unsigned opening_touch);
void arcade_import_draw(uint32_t *target,const char *stage,unsigned done,unsigned total);
#endif
