#ifndef TILT_MAZE_ARCADE_H
#define TILT_MAZE_ARCADE_H

#include <stdbool.h>
#include <stdint.h>

struct point {
	float x, y;
};

enum mode {
	MODE_MAZE,
	MODE_COINS,
	MODE_SURVIVAL,
	MODE_BALANCE,
	MODE_PINBALL,
	MODE_SPACE,
	MODE_SNAKE,
	MODE_COUNT,
};

enum enemy_kind {
	ENEMY_LINEAR,
	ENEMY_BOUNCE,
	ENEMY_CHASE,
};

enum {
	ARCADE_COIN_SLOTS = 5,
	ARCADE_ENEMY_SLOTS = 8,
	ARCADE_COIN_MS = 60000,
	ARCADE_ENEMY_RADIUS = 6,
	PIN_BUMPERS = 3,
	SPACE_ROCKS = 6,
	SPACE_SHOTS = 6,
	SNAKE_SEGMENTS = 48,
	SNAKE_LEFT = 55, SNAKE_RIGHT = 185, SNAKE_TOP = 63, SNAKE_BOTTOM = 193,
};

struct space_object {
	struct point at, velocity;
	uint8_t radius;
};

struct pinball_run {
	uint32_t flip_until;
	bool loaded;
};

struct space_run {
	struct space_object rocks[SPACE_ROCKS], shots[SPACE_SHOTS], pickup;
	uint32_t next_rock, next_pickup, fire_after;
	bool shield, pickup_shield;
};

struct snake_run {
	struct point body[SNAKE_SEGMENTS], food, heading;
	uint32_t step_at;
	uint8_t length;
	bool free_turn;
};

struct arcade_coin {
	struct point at;
	uint32_t respawn;
	bool bonus;
};

struct arcade_enemy {
	struct point at, velocity;
	enum enemy_kind kind;
};

struct arcade_run {
	struct arcade_coin coins[ARCADE_COIN_SLOTS];
	struct arcade_enemy enemies[ARCADE_ENEMY_SLOTS];
	struct point wind, flash;
	uint32_t score, last_coin, immune_until, flash_until;
	uint16_t collected, combo, peak;
	uint8_t lives, enemies_active, safe_radius, wind_id;
	union {
		struct pinball_run pinball;
		struct space_run space;
		struct snake_run snake;
	};
};

struct game;

void arcade_start(struct game *game, enum mode mode);
void arcade_tick(struct game *game, struct point tilt);
void arcade_trigger(struct game *game);
uint32_t arcade_best_score(const struct game *game, enum mode mode);
const char *arcade_name(enum mode mode);
extern const struct point pinball_bumpers[PIN_BUMPERS];

#endif
