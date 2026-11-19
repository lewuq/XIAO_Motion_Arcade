#ifndef TILT_MAZE_ADVENTURE_H
#define TILT_MAZE_ADVENTURE_H

#include <stdbool.h>
#include <stdint.h>

enum {
	WORLD_SIZE = 5,
	ADVENTURE_WORLDS = 5,
	ADVENTURE_LEVELS = WORLD_SIZE * ADVENTURE_WORLDS,
	ADVENTURE_DAYS = 16,
	CHALLENGE_MAX_TIER = 9,
};

enum skin {
	SKIN_GOLD, SKIN_ICE, SKIN_LAVA, SKIN_NEON, SKIN_PIXEL, SKIN_STAR, SKIN_COUNT,
};

enum achievement {
	ACH_FIRST, ACH_SPEED, ACH_PERFECT, ACH_COIN, ACH_SURVIVOR, ACH_MASTER,
	ACH_COLLECTOR, ACH_COMBO, ACH_COUNT,
};

enum expedition {
	EXP_STORY, EXP_RANDOM, EXP_DAILY,
};

struct daily_record {
	uint32_t date, time;
};

struct growth {
	struct daily_record days[ADVENTURE_DAYS];
	uint32_t date, legacy_unlocked, achievements, skins;
	uint8_t stars[ADVENTURE_LEVELS], skin;
};

struct game;

const char *adventure_world(unsigned int index);
const char *adventure_skin(unsigned int index);
const char *adventure_achievement(unsigned int index);
unsigned int adventure_skin_cost(unsigned int index);
unsigned int adventure_level(unsigned int position);
unsigned int adventure_position(unsigned int index);
unsigned int adventure_resume(const struct game *game);
unsigned int adventure_stars(const struct game *game);
void adventure_award(struct game *game, enum achievement achievement);
void adventure_complete(struct game *game);
bool adventure_buy(struct game *game, unsigned int skin);
bool adventure_equip(struct game *game, unsigned int skin);
bool adventure_date_valid(uint32_t date);
uint32_t adventure_date_adjust(uint32_t date, unsigned int field, int delta);
uint32_t adventure_daily_best(const struct game *game, uint32_t date);
bool challenge_valid(struct game *game);
void challenge_load(struct game *game, enum expedition type, uint32_t seed, unsigned int tier);

#endif
