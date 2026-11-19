#ifndef TILT_MAZE_H
#define TILT_MAZE_H

#include <stdbool.h>
#include <stdint.h>
#include "arcade.h"
#include "adventure.h"

enum {
	MAZE_WIDTH = 240,
	MAZE_HEIGHT = 240,
	MAZE_CENTER_X = 120,
	MAZE_CENTER_Y = 128,
	MAZE_RADIUS = 101,
	MAZE_BALL = 6,
	MAZE_WALL = 3,
	MAZE_LEVELS = ADVENTURE_LEVELS,
	MAZE_TICK_MS = 20,
	MAZE_MAX_WALLS = 48,
	MAZE_MAX_TRAPS = 4,
	MAZE_MAX_COINS = 12,
	MAZE_SKIN_UNLOCK = 12,
};

struct wall {
	struct point start, end;
};

struct trap {
	struct point center;
	int radius;
};

struct arc {
	uint8_t radius;
	uint16_t start, end;
};

enum mechanism {
	MECH_MOVING, MECH_ONE_WAY, MECH_PORTAL, MECH_LOW_G,
	MECH_ICE, MECH_BELT, MECH_ROTOR, MECH_KEY, MECH_DARK, MECH_REVERSE,
	MECH_TIMED, MECH_COUNT,
};

enum item_kind {
	ITEM_SHIELD, ITEM_SLOW, ITEM_MAGNET, ITEM_BLINK, ITEM_COUNT,
};

#define MAZE_BIT(index) (1u << (index))

struct feature {
	enum mechanism kind;
	struct point a, b;
	uint16_t radius, period;
	uint8_t id;
	bool door;
};

struct item {
	enum item_kind kind;
	struct point at, target;
};

struct level_spec {
	const char *name, *tip, *hint;
	struct point start, goal;
	const struct wall *walls;
	const struct trap *traps;
	const struct feature *features;
	const struct item *items;
	const struct point *coins;
	const struct arc *arcs;
	uint16_t target_seconds, all_bonus;
	uint8_t wall_count, trap_count, feature_count, item_count, coin_count, random_count, arc_count;
	uint8_t coin_target, expert_seconds;
};

struct level {
	const char *name;
	struct point start, goal;
	struct wall walls[MAZE_MAX_WALLS];
	struct trap traps[MAZE_MAX_TRAPS];
	unsigned int wall_count, trap_count;
	const struct level_spec *spec;
	bool mirrored;
};

enum page {
	PAGE_MENU,
	PAGE_CALIBRATE,
	PAGE_PLAY,
	PAGE_PAUSE,
	PAGE_WIN,
	PAGE_FAIL,
	PAGE_IMU_ERROR,
	PAGE_SELECT,
	PAGE_BRIEF,
	PAGE_QUIT,
	PAGE_ARCADE,
	PAGE_EXTRAS,
	PAGE_SKINS,
	PAGE_ACHIEVEMENTS,
	PAGE_CHALLENGES,
	PAGE_DATE,
	PAGE_SEED,
	PAGE_GENERATE,
	PAGE_WORLDS,
	PAGE_SETTINGS,
	PAGE_COUNTDOWN,
	PAGE_GAMES,
};

struct game_settings {
	uint8_t brightness, sensitivity;
	bool left_hand, rotated, quiet, snake_free;
};

struct run {
	uint32_t score, last_coin, slow_until, magnet_until, flash_until, react_until;
	struct point flash;
	uint16_t spawned, coins, keys, items, contacts;
	uint16_t gain;
	uint8_t combo, max_combo, portal_lock;
	bool shield;
	uint32_t safe_until, moving_inside;
};

struct game {
	struct level level;
	struct point ball, velocity;
	unsigned int index;
	enum page page;
	enum page skins_back;
	enum mode mode;
	enum mode selected_mode;
	uint32_t elapsed, record[MAZE_LEVELS];
	uint32_t high_score[MAZE_LEVELS], wallet, seed;
	uint16_t enabled, serial;
	struct run run;
	struct arcade_run arcade;
	uint32_t arcade_best[MODE_COUNT];
	uint32_t snake_free_best;
	uint8_t snake_best_length[2];
	struct game_settings settings;
	bool chinese;
	struct growth growth;
	uint32_t growth_revision, challenge_seed, challenge_date, edit_date, seed_input;
	enum expedition expedition;
	uint32_t random_streak;
	uint8_t stars, challenge_tier, collection_cursor, edit_field, countdown;
	bool new_record;
};

extern const struct level_spec maze_levels[MAZE_LEVELS];

void maze_load(struct game *game, unsigned int index);
void maze_leave(struct game *game);
void maze_tick(struct game *game, struct point tilt);
float maze_environment_time(const struct game *game, float time);
bool maze_gate_active(const struct game *game, const struct feature *feature);
bool maze_can_occupy(const struct level *level, struct point point, bool avoid_traps);
bool maze_route_open(const struct game *game, struct point from, struct point to);
bool maze_visible(const struct game *game, struct point at);
bool maze_feature_on(const struct game *game, const struct feature *feature);
struct point maze_mover(const struct feature *feature, float time);
struct wall maze_rotor(const struct feature *feature, float time);
unsigned int maze_coin_count(uint16_t mask);

static inline struct point maze_transform(const struct level *level, struct point point)
{
	if (level->mirrored) {
		point.x = 2 * MAZE_CENTER_X - point.x;
	}
	return point;
}

static inline struct feature maze_feature(const struct level *level, unsigned int index)
{
	struct feature feature = level->spec->features[index];

	if (level->mirrored) {
		feature.a = maze_transform(level, feature.a);
		if (feature.kind == MECH_BELT) {
			feature.b.x = -feature.b.x;
		} else {
			feature.b = maze_transform(level, feature.b);
		}
		if (feature.kind == MECH_ONE_WAY) {
			struct point start = feature.a;

			feature.a = feature.b;
			feature.b = start;
		}
	}
	return feature;
}

static inline struct item maze_item(const struct level *level, unsigned int index)
{
	struct item item = level->spec->items[index];

	item.at = maze_transform(level, item.at);
	item.target = maze_transform(level, item.target);
	return item;
}

static inline bool maze_has_key(const struct game *game, unsigned int id)
{
	return (game->run.keys & MAZE_BIT(id)) != 0;
}

#endif
