/* Per-cartridge frontend preferences. GPL-2.0-or-later. */
#ifndef H1_ARCADE_CONFIG_H
#define H1_ARCADE_CONFIG_H
#include <stdint.h>
enum {ARCADE_UP,ARCADE_DOWN,ARCADE_LEFT,ARCADE_RIGHT,ARCADE_A,ARCADE_B,ARCADE_C,ARCADE_D,ARCADE_COIN,ARCADE_START,ARCADE_BUTTONS};
typedef struct {unsigned mode,skip,volume,muted;uint8_t mapping[45];unsigned show_fps;} arcade_settings;
extern arcade_settings h1_arcade_settings;
extern unsigned h1_config_sequence,h1_config_writable;
void arcade_mapping_defaults(arcade_settings *);
int arcade_key_reserved(unsigned);
int arcade_map_button(arcade_settings *,unsigned,unsigned);
void arcade_config_load(arcade_settings *,const char *,unsigned);
int arcade_config_save(const arcade_settings *);
#endif
