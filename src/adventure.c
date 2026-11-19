#include "maze.h"

static const char *const worlds[] = {
	"MEADOW", "OCEAN", "FIRE CANYON", "THUNDER PARK", "STARFIELD",
};
static const char *const skins[] = {"CLASSIC GOLD", "ICE CRYSTAL", "LAVA", "NEON", "PIXEL", "STARFIELD"};
static const char *const achievements[] = {
	"FIRST WIN", "SPEED RUNNER", "PERFECT RUN", "COIN MASTER",
	"SURVIVOR", "MAZE MASTER", "COLLECTOR", "COMBO KING",
};
static const uint8_t campaign[MAZE_LEVELS] = {
	0, 1, 13, 7, 14,
	5, 4, 15, 16, 17,
	3, 6, 18, 19, 20,
	8, 11, 21, 22, 23,
	2, 9, 10, 12, 24,
};

const char *adventure_world(unsigned int index)
{
	return worlds[adventure_position(index) / WORLD_SIZE];
}

const char *adventure_skin(unsigned int index)
{
	return skins[index < SKIN_COUNT ? index : SKIN_GOLD];
}

const char *adventure_achievement(unsigned int index)
{
	return achievements[index < ACH_COUNT ? index : ACH_FIRST];
}

unsigned int adventure_skin_cost(unsigned int index)
{
	static const uint8_t costs[] = {0, 0, 0, 25, 40, 0};

	return index < SKIN_COUNT ? costs[index] : 0;
}

unsigned int adventure_position(unsigned int index)
{
	for (unsigned int position = 0; position < MAZE_LEVELS; position++) {
		if (campaign[position] == index) {
			return position;
		}
	}
	return 0;
}

unsigned int adventure_level(unsigned int position)
{
	return campaign[position % MAZE_LEVELS];
}

unsigned int adventure_resume(const struct game *game)
{
	for (unsigned int position = 0; position < MAZE_LEVELS; position++) {
		unsigned int index = campaign[position];

		if (!game->growth.stars[index]) {
			return index;
		}
	}
	return campaign[0];
}

void adventure_award(struct game *game, enum achievement achievement)
{
	if (!(game->growth.achievements & MAZE_BIT(achievement))) {
		game->growth.achievements |= MAZE_BIT(achievement);
		game->growth_revision++;
	}
}

static uint32_t unlocked_skins(const struct game *game)
{
	return game->growth.skins | MAZE_BIT(SKIN_GOLD);
}

static void unlock_skin(struct game *game, enum skin skin)
{
	if (!(unlocked_skins(game) & MAZE_BIT(skin))) {
		game->growth.skins |= MAZE_BIT(skin);
		game->growth_revision++;
		if (unlocked_skins(game) == MAZE_BIT(SKIN_COUNT) - 1u) {
			adventure_award(game, ACH_COLLECTOR);
		}
	}
}

bool adventure_buy(struct game *game, unsigned int skin)
{
	unsigned int cost = adventure_skin_cost(skin);

	if (!cost || (unlocked_skins(game) & MAZE_BIT(skin)) || game->wallet < cost) {
		return false;
	}
	game->wallet -= cost;
	unlock_skin(game, skin);
	return true;
}

bool adventure_equip(struct game *game, unsigned int skin)
{
	if (skin >= SKIN_COUNT || !(unlocked_skins(game) & MAZE_BIT(skin))) {
		return false;
	}
	if (skin != game->growth.skin) {
		game->growth.skin = skin;
		game->growth_revision++;
	}
	return true;
}

unsigned int adventure_stars(const struct game *game)
{
	const struct level_spec *spec = game->level.spec;
	unsigned int total = maze_coin_count(game->run.spawned);
	unsigned int collected = maze_coin_count(game->run.coins);
	unsigned int target = spec->coin_target ? spec->coin_target : (total + 1) / 2;
	unsigned int expert = spec->expert_seconds ? spec->expert_seconds : spec->target_seconds * 4 / 5;

	if (game->elapsed > spec->target_seconds * 1000u || collected < target) {
		return 1;
	}
	return game->run.contacts == 0 && game->elapsed <= expert * 1000u ? 3 : 2;
}

static unsigned int month_days(unsigned int year, unsigned int month)
{
	static const uint8_t lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

	return lengths[month - 1] + (month == 2 && year % 4 == 0);
}

bool adventure_date_valid(uint32_t date)
{
	unsigned int year = date / 10000, month = date / 100 % 100, day = date % 100;

	return year >= 2000 && year <= 2099 && month >= 1 && month <= 12 &&
	       day >= 1 && day <= month_days(year, month);
}

uint32_t adventure_date_adjust(uint32_t date, unsigned int field, int delta)
{
	int year = date / 10000;
	int month = date / 100 % 100;
	int day = date % 100;

	if (field == 0) {
		year = 2000 + (year - 2000 + delta + 100) % 100;
	} else if (field == 1) {
		month = 1 + (month - 1 + delta + 12) % 12;
	}
	int max_day = month_days(year, month);

	if (field == 2) {
		day = 1 + (day - 1 + delta + max_day) % max_day;
	} else if (day > max_day) {
		day = max_day;
	}
	return year * 10000u + month * 100u + day;
}

uint32_t adventure_daily_best(const struct game *game, uint32_t date)
{
	for (unsigned int idx = 0; idx < ADVENTURE_DAYS; idx++) {
		if (game->growth.days[idx].date == date) {
			return game->growth.days[idx].time;
		}
	}
	return 0;
}

static bool record_daily(struct game *game)
{
	struct daily_record *entries = game->growth.days;
	unsigned int slot = 0;

	for (unsigned int idx = 0; idx < ADVENTURE_DAYS; idx++) {
		if (entries[idx].date == game->challenge_date) {
			if (entries[idx].time <= game->elapsed) {
				return false;
			}
			slot = idx;
			break;
		}
		if (entries[idx].date < entries[slot].date) {
			slot = idx;
		}
	}
	entries[slot] = (struct daily_record){game->challenge_date, game->elapsed};
	return true;
}

void adventure_complete(struct game *game)
{
	struct growth *growth = &game->growth;
	unsigned int index = game->index;
	unsigned int position = adventure_position(index);
	unsigned int collected = maze_coin_count(game->run.coins);

	game->stars = adventure_stars(game);
	if (game->expedition == EXP_STORY) {
		game->new_record = game->record[index] == 0 || game->elapsed < game->record[index];
		if (game->new_record) {
			game->record[index] = game->elapsed;
		}
		if (game->run.score > game->high_score[index]) {
			game->high_score[index] = game->run.score;
		}
		if (game->stars > growth->stars[index]) {
			growth->stars[index] = game->stars;
		}
		unsigned int finished = 0;

		for (unsigned int idx = 0; idx < MAZE_LEVELS; idx++) {
			finished += growth->stars[idx] != 0;
		}
		if (finished == MAZE_LEVELS) {
			adventure_award(game, ACH_MASTER);
			unlock_skin(game, SKIN_STAR);
		}
		if (position == 14) {
			unlock_skin(game, SKIN_LAVA);
		}
	} else if (game->expedition == EXP_DAILY && adventure_date_valid(game->challenge_date)) {
		game->new_record = record_daily(game);
	} else if (game->expedition == EXP_RANDOM) {
		game->run.score += (game->challenge_tier + 1u) * 25u;
	}
	game->wallet += collected;
	adventure_award(game, ACH_FIRST);
	if (game->elapsed <= game->level.spec->target_seconds * 700u) {
		adventure_award(game, ACH_SPEED);
	}
	if (!game->run.contacts) {
		adventure_award(game, ACH_PERFECT);
	}
	if (game->run.spawned && game->run.coins == game->run.spawned) {
		adventure_award(game, ACH_COIN);
	}
	if (game->run.max_combo >= 5) {
		adventure_award(game, ACH_COMBO);
	}
	if (game->wallet >= MAZE_SKIN_UNLOCK) {
		unlock_skin(game, SKIN_ICE);
	}
	game->growth_revision++;
}
