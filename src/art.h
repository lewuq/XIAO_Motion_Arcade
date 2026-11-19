#ifndef TILT_MAZE_ART_H
#define TILT_MAZE_ART_H

#include "maze.h"

enum {
	ACTION_X = 63,
	ACTION_WIDTH = 114,
	PRIMARY_Y = 141,
	SECONDARY_Y = 178,
	ACTION_HEIGHT = 30,
	PAUSE_X = 168,
	PAUSE_Y = 31,
	PAUSE_WIDTH = 26,
	PAUSE_HEIGHT = 22,
	FOOTER_Y = 211,
	FOOTER_HEIGHT = 17,
	SPLIT_X = 57,
	SPLIT_W = 59,
	SPLIT_RIGHT = 124,
};

typedef int (*art_sink)(int x, int y, int width, int height, const uint16_t *pixels);

struct art_state {
	enum page page;
	enum mode mode, selected_mode;
	int ball_x, ball_y;
	unsigned int index, tenth, progress, pulse;
	uint32_t elapsed, flash_until, score;
	struct point flash;
	uint16_t serial, coins, items, keys, effects;
	uint8_t arena_enemies, arena_safe;
	struct point arena_enemy[ARCADE_ENEMY_SLOTS], arena_coin[ARCADE_COIN_SLOTS], arena_wind;
	uint8_t arena_coin_mask;
	bool arena_immune;
	uint8_t skin;
	uint32_t growth_revision, edit_value;
	uint32_t timed_gates;
	uint8_t collection_cursor, edit_field;
	uint8_t countdown;
	bool rotated;
	bool chinese;
	bool surprised;
	struct point motion_at[SNAKE_SEGMENTS + 1];
	uint8_t motion_size[SNAKE_SEGMENTS + 1];
	bool valid;
};

enum action {
	ACTION_NONE,
	ACTION_PRIMARY,
	ACTION_SECONDARY,
	ACTION_PAUSE,
	ACTION_PREV,
	ACTION_NEXT,
	ACTION_FOOTER,
	ACTION_REPLAY,
	ACTION_HOME,
	ACTION_EXTRAS,
	ACTION_TERTIARY,
	ACTION_EDIT,
	ACTION_FOURTH,
	ACTION_WORLDS,
	ACTION_LEVELS,
	ACTION_WORLD_1, ACTION_WORLD_2, ACTION_WORLD_3, ACTION_WORLD_4, ACTION_WORLD_5,
	ACTION_COIN_GAME, ACTION_DODGE_GAME, ACTION_BALANCE_GAME,
	ACTION_PINBALL_GAME, ACTION_SPACE_GAME, ACTION_SNAKE_GAME,
	ACTION_SETTINGS, ACTION_BRIGHTNESS, ACTION_SENSITIVITY, ACTION_HAND, ACTION_ROTATE, ACTION_QUIET,
	ACTION_SNAKE_STYLE, ACTION_TRIGGER,
	ACTION_SKINS,
	ACTION_LANGUAGE,
};

enum action art_action_at(const struct game *game, int x, int y);
int art_present(struct art_state *state, const struct game *game, unsigned int progress,
		unsigned int pulse, art_sink sink);

#endif
