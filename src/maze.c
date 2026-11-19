#include "maze.h"

#include <math.h>
#include <string.h>

#define TURN 6.28318530718f
#define GOAL_RADIUS 9
#define WALL_CLEARANCE ((float)(MAZE_BALL + MAZE_WALL))

#ifndef MAZE_DISABLED_MECHANISMS
#define MAZE_DISABLED_MECHANISMS 0u
#endif
#ifndef MAZE_COIN_POINTS
#define MAZE_COIN_POINTS 10u
#endif
#ifndef MAZE_COMBO_MS
#define MAZE_COMBO_MS 3000u
#endif
#ifndef MAZE_COMBO_FIRST
#define MAZE_COMBO_FIRST 3u
#endif
#ifndef MAZE_COMBO_SECOND
#define MAZE_COMBO_SECOND 5u
#endif
#ifndef MAZE_COMBO_FIRST_PERCENT
#define MAZE_COMBO_FIRST_PERCENT 150u
#endif
#ifndef MAZE_COMBO_SECOND_PERCENT
#define MAZE_COMBO_SECOND_PERCENT 200u
#endif
#ifndef MAZE_CLEAN_BONUS
#define MAZE_CLEAN_BONUS 100u
#endif

static float square(float value)
{
	return value * value;
}

static float norm_squared(struct point point)
{
	return square(point.x) + square(point.y);
}

static struct point subtract(struct point a, struct point b)
{
	return (struct point){a.x - b.x, a.y - b.y};
}

static float clamp(float value, float min, float max)
{
	return fmaxf(min, fminf(value, max));
}

static struct point wall_offset(const struct wall *wall, struct point position)
{
	struct point length = subtract(wall->end, wall->start);
	struct point offset = subtract(position, wall->start);
	float fraction = clamp((offset.x * length.x + offset.y * length.y) /
			       norm_squared(length), 0.0f, 1.0f);

	return (struct point){offset.x - length.x * fraction, offset.y - length.y * fraction};
}

static float dot(struct point a, struct point b)
{
	return a.x * b.x + a.y * b.y;
}

static struct point add_scaled(struct point a, struct point b, float scale)
{
	return (struct point){a.x + b.x * scale, a.y + b.y * scale};
}

static bool within(struct point a, struct point b, float radius)
{
	return norm_squared(subtract(a, b)) <= square(radius);
}

bool maze_feature_on(const struct game *game, const struct feature *feature)
{
	return (game->enabled & MAZE_BIT(feature->kind)) != 0;
}

unsigned int maze_coin_count(uint16_t mask)
{
	unsigned int count = 0;

	for (; mask; mask &= mask - 1u) {
		count++;
	}
	return count;
}

static void add_wall(struct level *level, struct point start, struct point end)
{
	level->walls[level->wall_count++] = (struct wall){start, end};
}

static void add_arc(struct level *level, float radius, int start, int end)
{
	int count = (end - start + 14) / 15;
	struct point previous = {0};

	for (int step = 0; step <= count; step++) {
		float angle = (start + (end - start) * (float)step / count) * TURN / 360;
		struct point point = {MAZE_CENTER_X + radius * cosf(angle),
				      MAZE_CENTER_Y + radius * sinf(angle)};

		if (step > 0) {
			add_wall(level, previous, point);
		}
		previous = point;
	}
}

void maze_load(struct game *game, unsigned int index)
{
	struct level *level = &game->level;

	game->mode = MODE_MAZE;
	game->expedition = EXP_STORY;
	game->stars = 0;
	game->challenge_date = 0;
	game->arcade = (struct arcade_run){0};
	game->index = index % MAZE_LEVELS;
	const struct level_spec *spec = &maze_levels[game->index];

	*level = (struct level){.spec = spec, .name = spec->name, .start = spec->start,
				.goal = spec->goal, .wall_count = spec->wall_count,
				.trap_count = spec->trap_count};
	if (spec->wall_count) {
		memcpy(level->walls, spec->walls, spec->wall_count * sizeof(*spec->walls));
	}
	if (spec->trap_count) {
		memcpy(level->traps, spec->traps, spec->trap_count * sizeof(*spec->traps));
	}
	for (unsigned int idx = 0; idx < spec->arc_count; idx++) {
		const struct arc *arc = &spec->arcs[idx];

		add_arc(level, arc->radius, arc->start, arc->end);
	}

	game->run = (struct run){.portal_lock = UINT8_MAX};
	game->enabled = (MAZE_BIT(MECH_COUNT) - 1u) & ~MAZE_DISABLED_MECHANISMS;
	game->serial++;
	game->run.spawned = (uint16_t)(MAZE_BIT(spec->coin_count) - 1u);
	for (unsigned int count = spec->coin_count; spec->random_count && count > spec->random_count; count--) {
		unsigned int picked;

		do {
			game->seed = game->seed * 1664525u + 1013904223u;
			picked = (game->seed >> 16) % spec->coin_count;
		} while (!(game->run.spawned & MAZE_BIT(picked)));
		game->run.spawned &= (uint16_t)~MAZE_BIT(picked);
	}
	game->ball = level->start;
	game->velocity = (struct point){0};
	game->elapsed = 0;
	game->new_record = false;
}

void maze_leave(struct game *game)
{
	game->page = PAGE_MENU;
	game->mode = MODE_MAZE;
	game->expedition = EXP_STORY;
	game->stars = 0;
	game->challenge_date = 0;
	game->random_streak = 0;
	game->arcade = (struct arcade_run){0};
	game->run = (struct run){.portal_lock = UINT8_MAX};
	game->elapsed = 0;
	game->ball = (struct point){0};
	game->velocity = (struct point){0};
	game->new_record = false;
}

struct point maze_mover(const struct feature *feature, float time)
{
	float phase = fmodf(time, feature->period) / feature->period;
	float fraction = 1.0f - fabsf(2.0f * phase - 1.0f);

	return add_scaled(feature->a, subtract(feature->b, feature->a), fraction);
}

struct wall maze_rotor(const struct feature *feature, float time)
{
	float phase = TURN * fmodf(time, feature->period) / feature->period;
	struct point direction = {cosf(phase), sinf(phase)};

	return (struct wall){add_scaled(feature->a, direction, -feature->radius),
			     add_scaled(feature->a, direction, feature->radius)};
}

float maze_environment_time(const struct game *game, float time)
{
	unsigned int tier = game->expedition == EXP_STORY ? 0 : game->challenge_tier;

	return time * (1.0f + tier * 0.085f);
}

static bool gate_active_at(const struct game *game, const struct feature *feature, struct point at)
{
	if (!maze_feature_on(game, feature)) {
		return false;
	}
	if (feature->kind == MECH_KEY && feature->door) {
		return !maze_has_key(game, feature->id);
	}
	if (feature->kind == MECH_TIMED && feature->period) {
		struct wall gate = {feature->a, feature->b};
		uint32_t phase = game->elapsed % feature->period;

		return phase < feature->period * 55u / 100u &&
		       norm_squared(wall_offset(&gate, at)) >= square(WALL_CLEARANCE);
	}
	return false;
}

bool maze_gate_active(const struct game *game, const struct feature *feature)
{
	return gate_active_at(game, feature, game->ball);
}

static bool in_trap(const struct level *level, struct point point)
{
	for (unsigned int index = 0; index < level->trap_count; index++) {
		const struct trap *trap = &level->traps[index];

		if (norm_squared(subtract(point, trap->center)) <= square(trap->radius + 2)) {
			return true;
		}
	}
	return false;
}

bool maze_can_occupy(const struct level *level, struct point point, bool avoid_traps)
{
	struct point relative = subtract(point, (struct point){MAZE_CENTER_X, MAZE_CENTER_Y});

	if (norm_squared(relative) > square(MAZE_RADIUS - MAZE_BALL) ||
	    (avoid_traps && in_trap(level, point))) {
		return false;
	}

	for (unsigned int index = 0; index < level->wall_count; index++) {
		if (norm_squared(wall_offset(&level->walls[index], point)) < square(WALL_CLEARANCE)) {
			return false;
		}
	}
	return true;
}

static bool closed_gate(const struct game *game, const struct feature *feature,
			struct point before, struct point after)
{
	if (!maze_feature_on(game, feature)) {
		return false;
	}
	if (feature->kind == MECH_KEY || feature->kind == MECH_TIMED) {
		return gate_active_at(game, feature, before);
	}
	if (feature->kind != MECH_ONE_WAY) {
		return false;
	}
	struct point length = subtract(feature->b, feature->a);
	struct point normal = {-length.y, length.x};

	return dot(subtract(before, feature->a), normal) >= 0 &&
	       dot(subtract(after, before), normal) < 0;
}

bool maze_visible(const struct game *game, struct point at)
{
	const struct level_spec *spec = game->level.spec;

	for (unsigned int idx = 0; idx < spec->feature_count; idx++) {
		struct feature configured = maze_feature(&game->level, idx);
		const struct feature *feature = &configured;

		if (feature->kind == MECH_DARK && maze_feature_on(game, feature)) {
			return within(game->ball, at, feature->radius);
		}
	}
	return true;
}

bool maze_route_open(const struct game *game, struct point from, struct point to)
{
	const struct level_spec *spec = game->level.spec;
	struct point travel = subtract(to, from);
	int steps = (int)ceilf(sqrtf(norm_squared(travel)));

	for (int step = 0; step <= steps; step++) {
		struct point position = add_scaled(from, travel, steps ? (float)step / steps : 0);

		if (!maze_can_occupy(&game->level, position, true)) {
			return false;
		}
		for (unsigned int idx = 0; idx < spec->feature_count; idx++) {
			struct feature configured = maze_feature(&game->level, idx);
			const struct feature *feature = &configured;
			struct wall gate = {feature->a, feature->b};
			bool blocked = closed_gate(game, feature, from, to);

			if (feature->kind == MECH_ONE_WAY && maze_feature_on(game, feature)) {
				struct point length = subtract(feature->b, feature->a);
				struct point normal = {-length.y, length.x};

				blocked |= dot(travel, normal) < 0;
			}
			if (blocked && norm_squared(wall_offset(&gate, position)) < square(WALL_CLEARANCE)) {
				return false;
			}
		}
	}
	return true;
}

static void resolve(struct game *game, struct point normal, float penetration, float bounce)
{
	float impact = dot(game->velocity, normal);

	game->ball = add_scaled(game->ball, normal, penetration);
	if (impact < 0) {
		game->velocity = add_scaled(game->velocity, normal, -(1 + bounce) * impact);
	}
}

static bool collide_wall(struct game *game, const struct wall *wall, float clearance, float bounce)
{
	struct point offset = wall_offset(wall, game->ball);
	float distance_squared = norm_squared(offset);

	if (distance_squared >= square(clearance)) {
		return false;
	}
	float depth = sqrtf(distance_squared);

	if (depth < 0.0001f) {
		struct point direction = subtract(wall->end, wall->start);

		offset = (struct point){-direction.y, direction.x};
		distance_squared = norm_squared(offset);
		if (dot(offset, game->velocity) > 0) {
			offset = add_scaled((struct point){0}, offset, -1);
		}
	}
	float inverse = 1.0f / sqrtf(distance_squared);

	resolve(game, (struct point){offset.x * inverse, offset.y * inverse},
		clearance - depth + 0.005f, bounce);
	return true;
}

static bool collide(struct game *game, struct point before)
{
	const struct level_spec *spec = game->level.spec;
	bool contact = false;

	for (unsigned int pass = 0; pass < 4; pass++) {
		bool collided = false;
		struct point relative = subtract(game->ball,
						  (struct point){MAZE_CENTER_X, MAZE_CENTER_Y});
		const float limit = MAZE_RADIUS - MAZE_BALL;

		if (norm_squared(relative) > square(limit)) {
			float distance = sqrtf(norm_squared(relative));

			resolve(game, (struct point){-relative.x / distance, -relative.y / distance},
				distance - limit + 0.005f, 0);
			collided = true;
		}
		for (unsigned int index = 0; index < game->level.wall_count; index++) {
			collided |= collide_wall(game, &game->level.walls[index], WALL_CLEARANCE, 0);
		}
		for (unsigned int idx = 0; idx < spec->feature_count; idx++) {
			struct feature configured = maze_feature(&game->level, idx);
			const struct feature *feature = &configured;
			struct wall gate = {feature->a, feature->b};

			if (closed_gate(game, feature, before, game->ball)) {
				if (feature->kind == MECH_ONE_WAY &&
				    norm_squared(wall_offset(&gate, game->ball)) < square(WALL_CLEARANCE)) {
					struct point axis = subtract(gate.end, gate.start);
					float inv = 1.0f / sqrtf(norm_squared(axis));
					struct point normal = {-axis.y * inv, axis.x * inv};
					float side = dot(subtract(game->ball, gate.start), normal);

					resolve(game, normal, WALL_CLEARANCE - side + 0.005f, 0);
					collided = true;
				} else {
					collided |= collide_wall(game, &gate, WALL_CLEARANCE, 0);
				}
			}
		}
		contact |= collided;
		if (!collided) {
			break;
		}
	}
	return contact;
}

static void effect(struct game *game, struct point at, unsigned int gain)
{
	game->run.flash = at;
	game->run.flash_until = game->elapsed + 420;
	game->run.gain = gain;
}

static bool danger(struct game *game, struct point before)
{
	if (game->elapsed < game->run.safe_until) {
		return false;
	}
	if (!game->run.shield) {
		game->page = PAGE_FAIL;
		return true;
	}
	game->run.react_until = game->elapsed + 300;
	game->run.shield = false;
	game->run.safe_until = game->elapsed + 650;
	effect(game, game->ball, 0);
	game->ball = before;
	game->velocity = (struct point){0};
	return false;
}

static bool moving_hit(const struct feature *feature, struct point before, struct point after,
		       float start, float stop)
{
	struct wall sweep = {subtract(before, maze_mover(feature, start)),
			     subtract(after, maze_mover(feature, stop))};
	float radius = MAZE_BALL + feature->radius;

	if (norm_squared(subtract(sweep.end, sweep.start)) < 0.00001f) {
		return norm_squared(sweep.start) <= square(radius);
	}
	return norm_squared(wall_offset(&sweep, (struct point){0})) <= square(radius);
}

static bool hazards(struct game *game, struct point before, float start, float stop, bool *contact)
{
	const struct level_spec *spec = game->level.spec;

	start = maze_environment_time(game, start);
	stop = maze_environment_time(game, stop);
	if (in_trap(&game->level, game->ball)) {
		*contact = true;
		if (danger(game, before)) {
			return false;
		}
	}
	for (unsigned int idx = 0; idx < spec->feature_count; idx++) {
		struct feature configured = maze_feature(&game->level, idx);
		const struct feature *feature = &configured;

		if (!maze_feature_on(game, feature)) {
			continue;
		}
		if (feature->kind == MECH_MOVING) {
			if (moving_hit(feature, before, game->ball, start, stop)) {
				*contact = true;
				if (!(game->run.moving_inside & MAZE_BIT(idx)) && danger(game, before)) {
					return false;
				}
			}
			if (within(game->ball, maze_mover(feature, stop), MAZE_BALL + feature->radius)) {
				game->run.moving_inside |= MAZE_BIT(idx);
			} else {
				game->run.moving_inside &= ~MAZE_BIT(idx);
			}
		} else if (feature->kind == MECH_ROTOR) {
			struct wall arm = maze_rotor(feature, stop);
			struct point old_velocity = game->velocity;
			float sweep = TURN * feature->radius * (stop - start) / feature->period;

			if (collide_wall(game, &arm, MAZE_BALL + 3 + sweep, 0.55f)) {
				struct point normal = wall_offset(&arm, game->ball);
				float distance = sqrtf(norm_squared(normal));

				if (distance > 0 && dot(game->velocity, normal) < distance * 34) {
					game->velocity = add_scaled(game->velocity, normal, 34 / distance);
				}
				collide(game, before);
				if (!maze_route_open(game, before, game->ball)) {
					game->ball = before;
					game->velocity = add_scaled((struct point){0}, old_velocity, -0.4f);
				}
				*contact = true;
			}
		} else if (feature->kind == MECH_KEY && !feature->door &&
			   !maze_has_key(game, feature->id) && within(game->ball, feature->a, 11)) {
			game->run.keys |= MAZE_BIT(feature->id);
			effect(game, feature->a, 0);
		}
	}
	return true;
}

static void use_portals(struct game *game)
{
	const struct level_spec *spec = game->level.spec;
	struct run *run = &game->run;

	if (run->portal_lock != UINT8_MAX) {
		struct feature configured = maze_feature(&game->level, run->portal_lock);
		const struct feature *portal = &configured;

		if (within(game->ball, portal->a, portal->radius + 2) ||
		    within(game->ball, portal->b, portal->radius + 2)) {
			return;
		}
		run->portal_lock = UINT8_MAX;
	}
	for (unsigned int idx = 0; idx < spec->feature_count; idx++) {
		struct feature configured = maze_feature(&game->level, idx);
		const struct feature *feature = &configured;

		if (feature->kind != MECH_PORTAL || !maze_feature_on(game, feature)) {
			continue;
		}
		bool forward = within(game->ball, feature->a, feature->radius);
		struct point target = forward ? feature->b : feature->a;

		if ((forward || within(game->ball, feature->b, feature->radius)) &&
		    maze_route_open(game, target, target)) {
			game->ball = target;
			game->velocity = (struct point){0};
			run->portal_lock = idx;
			return;
		}
	}
}

static void pickups(struct game *game)
{
	const struct level_spec *spec = game->level.spec;
	struct run *run = &game->run;

	for (unsigned int idx = 0; idx < spec->item_count; idx++) {
		struct item configured = maze_item(&game->level, idx);
		const struct item *item = &configured;

		if ((run->items & MAZE_BIT(idx)) || !within(game->ball, item->at, 11) ||
		    (item->kind == ITEM_BLINK && !maze_route_open(game, game->ball, item->target))) {
			continue;
		}
		run->items |= MAZE_BIT(idx);
		effect(game, item->at, 0);
		switch (item->kind) {
		case ITEM_SHIELD: run->shield = true; break;
		case ITEM_SLOW: run->slow_until = game->elapsed + 5000; break;
		case ITEM_MAGNET: run->magnet_until = game->elapsed + 5000; break;
		case ITEM_BLINK:
			game->ball = item->target;
			game->velocity = (struct point){0};
			break;
		default: break;
		}
	}
	for (unsigned int idx = 0; idx < spec->coin_count; idx++) {
		struct point at = maze_transform(&game->level, spec->coins[idx]);

		if (!(run->spawned & ~run->coins & MAZE_BIT(idx))) {
			continue;
		}
		bool near = within(game->ball, at, 10) ||
			(game->elapsed < run->magnet_until && within(game->ball, at, 30));

		if (!near || !maze_route_open(game, game->ball, at)) {
			continue;
		}
		run->coins |= MAZE_BIT(idx);
		run->combo++;
		if (run->combo > run->max_combo) {
			run->max_combo = run->combo;
		}
		unsigned int percent = run->combo >= MAZE_COMBO_SECOND ? MAZE_COMBO_SECOND_PERCENT :
			run->combo >= MAZE_COMBO_FIRST ? MAZE_COMBO_FIRST_PERCENT : 100;
		unsigned int gain = MAZE_COIN_POINTS * percent / 100;

		run->score += gain;
		run->last_coin = game->elapsed;
		effect(game, at, gain);
	}
}

static void finish(struct game *game)
{
	struct run *run = &game->run;

	if (run->contacts == 0) {
		run->score += MAZE_CLEAN_BONUS;
	}
	if (run->spawned && run->coins == run->spawned) {
		run->score += game->level.spec->all_bonus;
	}
	adventure_complete(game);
	game->page = PAGE_WIN;
}

void maze_tick(struct game *game, struct point tilt)
{
	const float dt = MAZE_TICK_MS / 1000.0f;
	struct run *run = &game->run;

	if (game->page != PAGE_PLAY || game->mode != MODE_MAZE) {
		return;
	}

	game->elapsed += MAZE_TICK_MS;
	if (game->elapsed - run->last_coin > MAZE_COMBO_MS) {
		run->combo = 0;
	}
	const struct level_spec *spec = game->level.spec;
	struct point belt = {0};
	float gravity = 1, friction = 0.952f;
	bool reversed = false;

	for (unsigned int idx = 0; idx < spec->feature_count; idx++) {
		struct feature configured = maze_feature(&game->level, idx);
		const struct feature *feature = &configured;

		if (!maze_feature_on(game, feature) || !within(game->ball, feature->a, feature->radius)) {
			continue;
		}
		switch (feature->kind) {
		case MECH_ICE: friction = 0.992f; break;
		case MECH_LOW_G: gravity = 0.42f; break;
		case MECH_REVERSE: reversed = true; break;
		case MECH_BELT: belt = add_scaled(belt, feature->b, 145); break;
		default: break;
		}
	}
	struct point force = add_scaled(belt, tilt, 560 * gravity * (reversed ? -1 : 1));
	float max_speed = game->elapsed < run->slow_until ? 58 : 130;

	game->velocity = add_scaled((struct point){0}, add_scaled(game->velocity, force, dt), friction);
	float speed_squared = norm_squared(game->velocity);

	if (speed_squared > square(max_speed)) {
		float scale = max_speed / sqrtf(speed_squared);

		game->velocity.x *= scale;
		game->velocity.y *= scale;
	}

	bool contact = false;
	unsigned int steps = spec->feature_count ? 4 : 3;

	for (unsigned int substep = 0; substep < steps; substep++) {
		struct point before = game->ball;

		game->ball = add_scaled(game->ball, game->velocity, dt / steps);
		contact |= collide(game, before);
		if (!maze_can_occupy(&game->level, game->ball, false)) {
			game->ball = before;
			game->velocity = (struct point){0};
		}
		float start = game->elapsed - MAZE_TICK_MS + (float)MAZE_TICK_MS * substep / steps;
		float stop = start + (float)MAZE_TICK_MS / steps;

		if (!hazards(game, before, start, stop, &contact)) {
			return;
		}
		use_portals(game);
		pickups(game);
		if (within(game->ball, game->level.goal, GOAL_RADIUS)) {
			run->contacts += contact && run->contacts < UINT16_MAX;
			finish(game);
			return;
		}
	}
	run->contacts += contact && run->contacts < UINT16_MAX;
	if (contact) {
		run->react_until = game->elapsed + 300;
	}
}
