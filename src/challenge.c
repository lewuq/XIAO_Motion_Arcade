#include "maze.h"

#include <math.h>
#include <string.h>

enum { GRID = 48, CELLS = GRID * GRID, MASKS = 4, SPACING = 5 };
static uint8_t free_cell[CELLS];
static uint8_t portal_enter[CELLS], portal_exit[CELLS];
static uint8_t keys[CELLS], doors[CELLS];
static uint16_t queue[MASKS * CELLS];
static uint8_t seen[MASKS][CELLS];

static float distance2(struct point a, struct point b)
{
	float dx = a.x - b.x, dy = a.y - b.y;

	return dx * dx + dy * dy;
}

static float segment2(struct point at, struct point a, struct point b)
{
	float dx = b.x - a.x, dy = b.y - a.y;
	float length = dx * dx + dy * dy;
	float ratio = length > 0 ? ((at.x - a.x) * dx + (at.y - a.y) * dy) / length : 0;

	ratio = fmaxf(0, fminf(1, ratio));
	return distance2(at, (struct point){a.x + dx * ratio, a.y + dy * ratio});
}

static struct point cell_point(unsigned int cell)
{
	return (struct point){(cell % GRID) * SPACING + 2, (cell / GRID) * SPACING + 2};
}

static bool clear_at(const struct game *game, struct point at)
{
	if (!maze_can_occupy(&game->level, at, true)) {
		return false;
	}
	for (unsigned int idx = 0; idx < game->level.spec->feature_count; idx++) {
		struct feature feature = maze_feature(&game->level, idx);
		float clearance = feature.radius + MAZE_BALL + 4;

		if (!maze_feature_on(game, &feature)) {
			continue;
		}
		if ((feature.kind == MECH_MOVING &&
		     segment2(at, feature.a, feature.b) < clearance * clearance) ||
		    (feature.kind == MECH_ROTOR && distance2(at, feature.a) < clearance * clearance)) {
			return false;
		}
	}
	return true;
}

static bool edge_allowed(struct game *game, struct point from, struct point to)
{
	struct point mid = {(from.x + to.x) / 2, (from.y + to.y) / 2};

	return clear_at(game, mid) && maze_route_open(game, from, to);
}

bool challenge_valid(struct game *game)
{
	const struct level_spec *spec = game->level.spec;
	uint16_t old_keys = game->run.keys;
	uint32_t old_time = game->elapsed;
	unsigned int head = 0, tail = 0;
	uint16_t exits[32] = {0};
	unsigned int portal_count = 0;
	bool valid = false;

	memset(seen, 0, sizeof(seen));
	memset(keys, 0, sizeof(keys));
	memset(doors, 0, sizeof(doors));
	memset(portal_enter, 0, sizeof(portal_enter));
	memset(portal_exit, 0, sizeof(portal_exit));
	game->elapsed = 2000;
	game->run.keys = 0;
	if (!clear_at(game, game->level.start)) {
		goto restore;
	}
	for (unsigned int cell = 0; cell < CELLS; cell++) {
		free_cell[cell] = clear_at(game, cell_point(cell));
	}
	for (unsigned int idx = 0; idx < spec->feature_count; idx++) {
		struct feature feature = maze_feature(&game->level, idx);
		unsigned int pair = portal_count;

		if (!maze_feature_on(game, &feature)) {
			continue;
		}
		if (feature.kind == MECH_PORTAL) {
			if (portal_count + 2 > sizeof(exits) / sizeof(exits[0])) {
				goto restore;
			}
			portal_count += 2;
			for (unsigned int end = 0; end < 2; end++) {
				struct point target = end ? feature.b : feature.a;
				float closest = feature.radius * feature.radius;
				bool found = false;

				if (!clear_at(game, target)) {
					goto restore;
				}

				for (unsigned int cell = 0; cell < CELLS; cell++) {
					float distance = distance2(cell_point(cell), target);

					if (free_cell[cell] && distance <= closest &&
					    edge_allowed(game, target, cell_point(cell))) {
						closest = distance;
						exits[pair + end] = cell;
						found = true;
					}
				}
				if (!found) {
					goto restore;
				}
			}
		}
		for (unsigned int cell = 0; cell < CELLS; cell++) {
			struct point at = cell_point(cell);

			if (!free_cell[cell]) {
				continue;
			}
			if (feature.kind == MECH_PORTAL) {
				if (distance2(at, feature.a) <= feature.radius * feature.radius) {
					portal_enter[cell] = pair + 2;
					portal_exit[cell] = pair + 1;
				} else if (distance2(at, feature.b) <= feature.radius * feature.radius) {
					portal_enter[cell] = pair + 1;
					portal_exit[cell] = pair + 2;
				}
			} else if (feature.kind == MECH_KEY && feature.id < 2) {
				if (feature.door && segment2(at, feature.a, feature.b) < 81) {
					doors[cell] |= MAZE_BIT(feature.id);
				} else if (!feature.door && distance2(at, feature.a) <= 100 &&
					   maze_route_open(game, at, feature.a)) {
					keys[cell] |= MAZE_BIT(feature.id);
				}
			}
		}
	}
	for (unsigned int cell = 0; cell < CELLS; cell++) {
		if (free_cell[cell] && !doors[cell] && !portal_enter[cell] &&
		    distance2(cell_point(cell), game->level.start) <= 36 &&
		    edge_allowed(game, game->level.start, cell_point(cell))) {
			seen[0][cell] = 1;
			queue[tail++] = cell;
		}
	}
	static const int moves[][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

	while (head < tail) {
		unsigned int state = queue[head++];
		unsigned int cell = state % CELLS;
		unsigned int held = state / CELLS;
		struct point at = cell_point(cell);

		game->run.keys = held;
		if (distance2(at, game->level.goal) <= 81) {
			valid = true;
			break;
		}
		for (unsigned int direction = 0; direction < 4; direction++) {
			int col = (int)(cell % GRID) + moves[direction][0];
			int row = (int)(cell / GRID) + moves[direction][1];

			if (col < 0 || row < 0 || col >= GRID || row >= GRID) {
				continue;
			}
			unsigned int next = row * GRID + col;
			unsigned int next_keys = held | keys[next];

			if (!free_cell[next] || (doors[next] & ~held)) {
				continue;
			}
			unsigned int entry = next;

			if (portal_enter[next] && portal_exit[cell] != portal_exit[next]) {
				next = exits[portal_enter[next] - 1];
				next_keys |= keys[next];
			}
			if (!seen[next_keys][next] && edge_allowed(game, at, cell_point(entry))) {
				seen[next_keys][next] = 1;
				queue[tail++] = next_keys * CELLS + next;
			}
		}
	}
restore:
	game->elapsed = old_time;
	game->run.keys = old_keys;
	return valid;
}

static uint32_t mix(uint32_t seed)
{
	seed ^= seed >> 16;
	seed *= 0x7feb352du;
	seed ^= seed >> 15;
	seed *= 0x846ca68bu;
	return seed ^ (seed >> 16);
}

static void mirror_layout(struct level *level)
{
	level->mirrored = true;
	level->start = maze_transform(level, level->start);
	level->goal = maze_transform(level, level->goal);
	for (unsigned int idx = 0; idx < level->wall_count; idx++) {
		level->walls[idx].start = maze_transform(level, level->walls[idx].start);
		level->walls[idx].end = maze_transform(level, level->walls[idx].end);
	}
	for (unsigned int idx = 0; idx < level->trap_count; idx++) {
		level->traps[idx].center = maze_transform(level, level->traps[idx].center);
	}
}

void challenge_load(struct game *game, enum expedition type, uint32_t seed, unsigned int tier)
{
	static const uint8_t templates[][6] = {
		{0, 1, 5, 8, 13, 21}, {3, 4, 6, 7, 10, 16}, {12, 14, 15, 17, 19, 24},
	};
	static const struct trap bonus[] = {
		{{77, 78}, 5}, {{159, 74}, 5}, {{58, 134}, 6}, {{184, 149}, 6},
		{{91, 183}, 5}, {{151, 193}, 5}, {{118, 110}, 5}, {{129, 164}, 5},
	};
	uint32_t original = seed;
	tier = tier > CHALLENGE_MAX_TIER ? CHALLENGE_MAX_TIER : tier;
	unsigned int group = tier < 3 ? 0 : tier < 6 ? 1 : 2;
	bool ready = false;

	game->seed = seed;
	for (unsigned int attempt = 0; attempt < 4 && !ready; attempt++) {
		seed = mix(seed + 0x9e3779b9u);
		maze_load(game, templates[group][seed % 6]);
		if (seed & 0x100) {
			mirror_layout(&game->level);
		}
		unsigned int count = game->level.trap_count;
		unsigned int additions = tier / 3;

		for (unsigned int step = 0; step < additions && game->level.trap_count < MAZE_MAX_TRAPS; step++) {
			struct trap trap = bonus[(seed + step * 5u) % (sizeof(bonus) / sizeof(bonus[0]))];

			trap.center = maze_transform(&game->level, trap.center);
			if (distance2(trap.center, game->level.start) > 576 &&
			    distance2(trap.center, game->level.goal) > 576 &&
			    maze_can_occupy(&game->level, trap.center, true)) {
				game->level.traps[game->level.trap_count++] = trap;
			}
		}
		ready = challenge_valid(game);
		if (!ready && game->level.trap_count != count) {
			game->level.trap_count = count;
			ready = challenge_valid(game);
		}
	}
	if (!ready) {
		/* The unmodified introductory template is the guaranteed fallback. */
		maze_load(game, 0);
	}
	game->expedition = type;
	game->challenge_seed = original;
	game->challenge_tier = tier;
	game->challenge_date = type == EXP_DAILY ? original : 0;
	game->ball = game->level.start;
}
