#include "maze.h"

#include <math.h>
#include <stdlib.h>

static const struct point center = {MAZE_CENTER_X, MAZE_CENTER_Y};
const struct point pinball_bumpers[PIN_BUMPERS] = {{86, 104}, {120, 72}, {154, 104}};

static struct point add(struct point a, struct point b, float scale)
{
	return (struct point){a.x + b.x * scale, a.y + b.y * scale};
}

static float norm2(struct point at)
{
	return at.x * at.x + at.y * at.y;
}

static bool near(struct point a, struct point b, float radius)
{
	return norm2(add(a, b, -1)) <= radius * radius;
}

static struct point unit(struct point at)
{
	float length = sqrtf(norm2(at));

	return length > 0.0001f ? add((struct point){0}, at, 1 / length) : (struct point){1, 0};
}

static unsigned int random_value(struct game *game, unsigned int limit)
{
	game->seed = game->seed * 1664525u + 1013904223u;
	return (game->seed >> 16) % limit;
}

static struct point heading(struct game *game)
{
	float angle = random_value(game, 1024) * (6.2831853f / 1024);

	return (struct point){cosf(angle), sinf(angle)};
}

static void contain(struct point *at, struct point *velocity, float radius, bool bounce)
{
	struct point offset = add(*at, center, -1);

	if (norm2(offset) <= radius * radius) {
		return;
	}
	struct point normal = unit(offset);
	float outward = velocity->x * normal.x + velocity->y * normal.y;

	*at = add(center, normal, radius - 0.01f);
	if (outward > 0) {
		*velocity = add(*velocity, normal, -(bounce ? 2 : 1) * outward);
	}
}

static void spawn_coin(struct game *game, unsigned int slot)
{
	struct arcade_coin *coin = &game->arcade.coins[slot];

	for (unsigned int attempt = 0; attempt < 20; attempt++) {
		struct point at = {MAZE_CENTER_X + (int)random_value(game, 161) - 80,
				   MAZE_CENTER_Y + (int)random_value(game, 151) - 75};
		bool occupied = near(at, game->ball, 23) || !near(at, center, 79);

		for (unsigned int other = 0; other < ARCADE_COIN_SLOTS; other++) {
			if (other != slot && !game->arcade.coins[other].respawn) {
				occupied |= near(at, game->arcade.coins[other].at, 15);
			}
		}
		if (!occupied) {
			coin->at = at;
			coin->bonus = random_value(game, 7) == 0;
			coin->respawn = 0;
			return;
		}
	}
	coin->respawn = game->elapsed + 200;
}

static void spawn_enemy(struct game *game, struct arcade_enemy *enemy, unsigned int slot)
{
	struct point direction = heading(game);
	struct point at = add(center, direction, 87);

	if (near(at, game->ball, 35)) {
		direction = add((struct point){0}, direction, -1);
		at = add(center, direction, 87);
	}
	enemy->kind = slot % 3;
	enemy->at = at;
	enemy->velocity = add((struct point){0}, direction, enemy->kind == ENEMY_LINEAR ? -1 : 0);
	if (enemy->kind != ENEMY_LINEAR) {
		enemy->velocity = unit(add(heading(game), direction, -1.1f));
	}
}

static void ensure_enemies(struct game *game)
{
	struct arcade_run *run = &game->arcade;
	uint32_t target = game->mode == MODE_SURVIVAL ? 2 + game->elapsed / 12000 :
		1 + game->elapsed / 15000;

	while (run->enemies_active < target && run->enemies_active < ARCADE_ENEMY_SLOTS) {
		unsigned int slot = run->enemies_active++;

		spawn_enemy(game, &run->enemies[slot], slot);
	}
}

const char *arcade_name(enum mode mode)
{
	switch (mode) {
	case MODE_COINS: return "COIN RUSH";
	case MODE_SURVIVAL: return "DODGE SURVIVAL";
	case MODE_BALANCE: return "STAY CENTERED";
	case MODE_PINBALL: return "TILT PINBALL";
	case MODE_SPACE: return "SPACE RUN";
	case MODE_SNAKE: return "TILT SNAKE";
	default: return "TILT MAZE";
	}
}

static bool snake_food(struct game *game)
{
	struct snake_run *snake = &game->arcade.snake;
	const unsigned int grid = 12, cells = grid * grid;
	unsigned int start = random_value(game, cells);

	for (unsigned int step = 0; step < cells; step++) {
		unsigned int index = (start + step) % cells;
		struct point at = {SNAKE_LEFT + 10 + (index % grid) * 10,
				   SNAKE_TOP + 10 + (index / grid) * 10};
		bool occupied = false;

		for (unsigned int segment = 0; segment < snake->length; segment++) {
			occupied |= near(at, snake->body[segment], snake->free_turn ? 13 : 8);
		}
		if (!occupied) {
			snake->food = at;
			return true;
		}
	}
	return false;
}

void arcade_start(struct game *game, enum mode mode)
{
	maze_leave(game);
	game->mode = mode;
	game->serial++;
	game->ball = center;
	game->page = PAGE_PLAY;
	switch (mode) {
	case MODE_COINS:
		for (unsigned int slot = 0; slot < ARCADE_COIN_SLOTS; slot++) {
			spawn_coin(game, slot);
		}
		ensure_enemies(game);
		break;
	case MODE_SURVIVAL:
		game->arcade.lives = 3;
		ensure_enemies(game);
		break;
	case MODE_BALANCE:
		game->arcade.safe_radius = 49;
		break;
	case MODE_PINBALL:
		game->arcade.lives = 3;
		game->arcade.pinball.loaded = true;
		game->ball = (struct point){120, 161};
		break;
	case MODE_SPACE:
		game->arcade.lives = 3;
		game->arcade.space.next_rock = 300;
		game->arcade.space.next_pickup = 5400;
		game->ball = (struct point){120, 167};
		break;
	case MODE_SNAKE: {
		struct snake_run *snake = &game->arcade.snake;

		snake->length = 4;
		snake->free_turn = game->settings.snake_free;
		snake->heading = (struct point){1, 0};
		snake->step_at = 170;
		for (unsigned int idx = 0; idx < snake->length; idx++) {
			snake->body[idx] = (struct point){115 - (int)idx * 10, 123};
		}
		game->ball = snake->body[0];
		snake_food(game);
		break;
	}
	default:
		break;
	}
}

uint32_t arcade_best_score(const struct game *game, enum mode mode)
{
	if (mode == MODE_SNAKE && game->settings.snake_free) {
		return game->snake_free_best;
	}
	return mode < MODE_COUNT ? game->arcade_best[mode] : 0;
}

static void conclude(struct game *game, enum page page)
{
	bool free_snake = game->mode == MODE_SNAKE && game->arcade.snake.free_turn;
	uint32_t *best = free_snake ? &game->snake_free_best : &game->arcade_best[game->mode];

	game->new_record = game->arcade.score > *best;
	if (game->new_record) {
		*best = game->arcade.score;
		game->growth_revision++;
	}
	if (game->mode == MODE_COINS && game->arcade.peak >= 5) {
		adventure_award(game, ACH_COMBO);
	}
	if (game->mode == MODE_SNAKE && game->arcade.snake.length > game->snake_best_length[free_snake]) {
		game->snake_best_length[free_snake] = game->arcade.snake.length;
		game->growth_revision++;
		game->new_record = true;
	}
	game->page = page;
}

static void collect_coins(struct game *game)
{
	struct arcade_run *run = &game->arcade;

	if (game->elapsed - run->last_coin > 3000) {
		run->combo = 0;
	}
	for (unsigned int slot = 0; slot < ARCADE_COIN_SLOTS; slot++) {
		struct arcade_coin *coin = &run->coins[slot];

		if (coin->respawn && game->elapsed >= coin->respawn) {
			spawn_coin(game, slot);
		}
		if (coin->respawn || !near(coin->at, game->ball, MAZE_BALL + 4)) {
			continue;
		}
		run->collected++;
		if (++run->combo > run->peak) {
			run->peak = run->combo;
		}
		unsigned int value = coin->bonus ? 30 : 10;

		run->score += run->combo >= 5 ? value * 2 : run->combo >= 3 ? value * 3 / 2 : value;
		run->last_coin = game->elapsed;
		run->flash_until = game->elapsed + 240;
		run->flash = coin->at;
		coin->respawn = game->elapsed + 550;
	}
}

static void advance_enemies(struct game *game, float dt)
{
	struct arcade_run *run = &game->arcade;
	float speed = 40.0f + fminf(game->elapsed / 1300.0f, 57.0f);

	for (unsigned int slot = 0; slot < run->enemies_active; slot++) {
		struct arcade_enemy *enemy = &run->enemies[slot];

		if (enemy->kind == ENEMY_CHASE) {
			struct point desired = unit(add(game->ball, enemy->at, -1));

			enemy->velocity = unit(add(enemy->velocity, add(desired, enemy->velocity, -1), dt * 3));
		}
		enemy->at = add(enemy->at, enemy->velocity, speed * dt *
				(enemy->kind == ENEMY_CHASE ? 0.80f : 1));
		if (enemy->kind == ENEMY_LINEAR) {
			if (!near(enemy->at, center, MAZE_RADIUS + ARCADE_ENEMY_RADIUS)) {
				spawn_enemy(game, enemy, slot);
			}
		} else {
			contain(&enemy->at, &enemy->velocity, MAZE_RADIUS - ARCADE_ENEMY_RADIUS, true);
		}
		if (game->elapsed >= run->immune_until &&
		    near(enemy->at, game->ball, MAZE_BALL + ARCADE_ENEMY_RADIUS)) {
			run->immune_until = game->elapsed + 1000;
			if (game->mode == MODE_SURVIVAL) {
				if (--run->lives == 0) {
					conclude(game, PAGE_FAIL);
					return;
				}
			} else {
				run->score = run->score >= 25 ? run->score - 25 : 0;
				run->combo = 0;
			}
		}
	}
}

static struct point balance_wind(struct game *game)
{
	struct arcade_run *run = &game->arcade;
	uint32_t stage = game->elapsed / 10000;
	uint32_t cycle = game->elapsed / 3200;
	uint32_t part = game->elapsed % 3200;
	uint8_t pulse = (uint8_t)(cycle % 254 + 1);
	static const struct point directions[] = {{1, 0}, {0, -1}, {-1, 0}, {0, 1},
		{0.707f, 0.707f}, {-0.707f, -0.707f}, {-0.707f, 0.707f}, {0.707f, -0.707f}};

	run->safe_radius = stage < 7 ? 49 - stage * 4 : 21;
	if (part < 2000) {
		run->wind = (struct point){0};
		return run->wind;
	}
	if (run->wind_id != pulse || norm2(run->wind) == 0) {
		run->wind_id = pulse;
		run->wind = directions[random_value(game, 8)];
	}
	return part >= 2350 ? add((struct point){0}, run->wind, 92 + fminf(stage, 10) * 13) :
		(struct point){0};
}

static struct point limit_speed(struct point velocity, float speed)
{
	return norm2(velocity) > speed * speed ? add((struct point){0}, unit(velocity), speed) : velocity;
}

static void impact(struct game *game, struct point at)
{
	game->arcade.flash = at;
	game->arcade.flash_until = game->elapsed + 240;
}

void arcade_trigger(struct game *game)
{
	if (game->page != PAGE_PLAY) {
		return;
	}
	if (game->mode == MODE_PINBALL) {
		struct pinball_run *pin = &game->arcade.pinball;

		if (game->elapsed < pin->flip_until) {
			return;
		}
		pin->flip_until = game->elapsed + 180;
		if (pin->loaded) {
			pin->loaded = false;
			game->velocity = (struct point){(int)random_value(game, 77) - 38, -215};
		}
	} else if (game->mode == MODE_SPACE) {
		struct space_run *space = &game->arcade.space;

		if (game->elapsed < space->fire_after) {
			return;
		}
		for (unsigned int idx = 0; idx < SPACE_SHOTS; idx++) {
			if (!space->shots[idx].radius) {
				space->shots[idx] = (struct space_object){
					.at = {game->ball.x, game->ball.y - 7}, .velocity = {0, -192}, .radius = 2,
				};
				space->fire_after = game->elapsed + 160;
				break;
			}
		}
	}
}

static void pinball_tick(struct game *game, struct point tilt, float dt)
{
	struct arcade_run *run = &game->arcade;
	struct pinball_run *pin = &run->pinball;
	struct point force = {tilt.x * 290, 123 + tilt.y * 260};

	if (pin->loaded) {
		return;
	}
	if (game->elapsed - run->last_coin > 3200) {
		run->combo = 0;
	}
	game->velocity = limit_speed(add(game->velocity, force, dt), 226);
	for (unsigned int step = 0; step < 4; step++) {
		game->ball = add(game->ball, game->velocity, dt * 0.25f);
		if (game->ball.y >= 192 && game->ball.y < 211 && fabsf(game->ball.x - 120) <= 33 &&
		    game->velocity.y > 0 && game->elapsed < pin->flip_until) {
			game->velocity = (struct point){(120 - game->ball.x) * 3.2f, -208};
		}
		if (game->ball.y >= 211 && fabsf(game->ball.x - 120) <= 22) {
			impact(game, game->ball);
			if (--run->lives == 0) {
				conclude(game, PAGE_FAIL);
			} else {
				game->ball = (struct point){120, 161};
				game->velocity = (struct point){0};
				pin->loaded = true;
				run->combo = 0;
			}
			return;
		}
		contain(&game->ball, &game->velocity, MAZE_RADIUS - MAZE_BALL, true);
		for (unsigned int idx = 0; idx < PIN_BUMPERS; idx++) {
			struct point at = pinball_bumpers[idx];
			struct point offset = add(game->ball, at, -1);
			const float clearance = MAZE_BALL + 11;

			if (norm2(offset) >= clearance * clearance) {
				continue;
			}
			struct point normal = unit(offset);
			float inward = game->velocity.x * normal.x + game->velocity.y * normal.y;

			game->ball = add(at, normal, clearance + 0.06f);
			if (inward >= 0) {
				continue;
			}
			game->velocity = add(game->velocity, normal, -inward + fmaxf(112, -inward * 1.12f));
			if (run->combo < 5) {
				run->combo++;
			}
			if (run->combo > run->peak) {
				run->peak = run->combo;
			}
			run->score += run->combo * 50u;
			run->last_coin = game->elapsed;
			impact(game, at);
		}
	}
}

static void space_spawn(struct game *game)
{
	struct space_run *space = &game->arcade.space;

	if (game->elapsed >= space->next_rock) {
		space->next_rock = game->elapsed + 1050 - (uint32_t)fminf(game->elapsed / 170.0f, 600);
		for (unsigned int idx = 0; idx < SPACE_ROCKS; idx++) {
			if (!space->rocks[idx].radius) {
				int side = (int)random_value(game, 125) - 62;
				struct point at = {120 + side, 49 + abs(side) / 3};

				if (near(at, game->ball, 31)) {
					at = (struct point){game->ball.x < 120 ? 175 : 65, 66};
				}
				space->rocks[idx] = (struct space_object){.at = at,
					.velocity = {(int)random_value(game, 39) - 19,
						50 + random_value(game, 24) + fminf(game->elapsed / 1200.0f, 64)},
					.radius = 7 + random_value(game, 7)};
				break;
			}
		}
	}
	if (game->elapsed >= space->next_pickup && !space->pickup.radius) {
		space->next_pickup = game->elapsed + 8500;
		space->pickup_shield = random_value(game, 2) != 0;
		space->pickup = (struct space_object){
			.at = {82 + random_value(game, 77), 57}, .velocity = {0, 36}, .radius = 5,
		};
	}
}

static void space_tick(struct game *game, struct point force, float dt)
{
	struct arcade_run *run = &game->arcade;
	struct space_run *space = &run->space;

	game->velocity = limit_speed(add((struct point){0}, add(game->velocity, force, dt), 0.902f), 126);
	game->ball = add(game->ball, game->velocity, dt);
	contain(&game->ball, &game->velocity, MAZE_RADIUS - 12, false);
	space_spawn(game);
	for (unsigned int idx = 0; idx < SPACE_SHOTS; idx++) {
		struct space_object *shot = &space->shots[idx];

		if (shot->radius) {
			shot->at = add(shot->at, shot->velocity, dt);
			if (!near(shot->at, center, MAZE_RADIUS + 1)) {
				shot->radius = 0;
			}
		}
	}
	for (unsigned int idx = 0; idx < SPACE_ROCKS; idx++) {
		struct space_object *rock = &space->rocks[idx];

		if (!rock->radius) {
			continue;
		}
		rock->at = add(rock->at, rock->velocity, dt);
		if (!near(rock->at, center, MAZE_RADIUS + 3)) {
			rock->radius = 0;
			continue;
		}
		for (unsigned int i = 0; i < SPACE_SHOTS; i++) {
			struct space_object *shot = &space->shots[i];

			if (shot->radius && near(shot->at, rock->at, shot->radius + rock->radius)) {
				run->score += 35 + rock->radius * 5;
				run->collected++;
				impact(game, rock->at);
				rock->radius = 0;
				shot->radius = 0;
				break;
			}
		}
		if (rock->radius && game->elapsed >= run->immune_until &&
		    near(game->ball, rock->at, rock->radius + 6)) {
			impact(game, rock->at);
			rock->radius = 0;
			run->immune_until = game->elapsed + 950;
			if (space->shield) {
				space->shield = false;
			} else if (--run->lives == 0) {
				conclude(game, PAGE_FAIL);
				return;
			}
		}
	}
	if (space->pickup.radius) {
		space->pickup.at = add(space->pickup.at, space->pickup.velocity, dt);
		if (near(space->pickup.at, game->ball, 11)) {
			space->shield |= space->pickup_shield;
			run->score += space->pickup_shield ? 0 : 120;
			impact(game, space->pickup.at);
			space->pickup.radius = 0;
		} else if (space->pickup.at.y > 214) {
			space->pickup.radius = 0;
		}
	}
}

static struct point snake_direction(struct snake_run *snake, struct point tilt, float dt)
{
	struct point heading = snake->heading;

	if (norm2(tilt) < 0.23f * 0.23f) {
		return heading;
	}
	if (snake->free_turn) {
		float angle = atan2f(heading.x * tilt.y - heading.y * tilt.x,
				     heading.x * tilt.x + heading.y * tilt.y);

		angle = fmaxf(-4.0f * dt, fminf(4.0f * dt, angle));
		float x = cosf(angle), y = sinf(angle);

		return (struct point){heading.x * x - heading.y * y, heading.x * y + heading.y * x};
	}
	struct point direction = fabsf(tilt.x) > fabsf(tilt.y) ?
		(struct point){copysignf(1, tilt.x), 0} : (struct point){0, copysignf(1, tilt.y)};

	return norm2(add(heading, direction, 1)) < 1 ? heading : direction;
}

static void snake_tick(struct game *game, struct point tilt, float dt)
{
	struct snake_run *snake = &game->arcade.snake;

	if (!snake->free_turn && game->elapsed < snake->step_at) {
		return;
	}
	snake->heading = snake_direction(snake, tilt, dt);
	snake->step_at = game->elapsed + 170 - (unsigned int)fminf(snake->length * 1.8f, 57);
	struct point head = add(game->ball, snake->heading,
				snake->free_turn ? (58 + fminf(snake->length, 30)) * dt : 10);
	bool ate = near(head, snake->food, snake->free_turn ? 9 : 5);
	bool hit = head.x < SNAKE_LEFT - 0.01f || head.x > SNAKE_RIGHT + 0.01f ||
		   head.y < SNAKE_TOP - 0.01f || head.y > SNAKE_BOTTOM + 0.01f;
	unsigned int last = snake->length - (!ate && !snake->free_turn);

	for (unsigned int idx = snake->free_turn ? 4 : 1; idx < last; idx++) {
		hit |= near(head, snake->body[idx], snake->free_turn ? 7 : 4);
	}
	if (hit) {
		conclude(game, PAGE_FAIL);
		return;
	}
	struct point tail = snake->body[snake->length - 1];

	if (snake->free_turn) {
		snake->body[0] = head;
		for (unsigned int idx = 1; idx < snake->length; idx++) {
			struct point offset = add(snake->body[idx], snake->body[idx - 1], -1);

			if (norm2(offset) > 64) {
				snake->body[idx] = add(snake->body[idx - 1], unit(offset), 8);
			}
		}
	} else {
		for (unsigned int idx = snake->length - 1; idx; idx--) {
			snake->body[idx] = snake->body[idx - 1];
		}
		snake->body[0] = head;
	}
	game->ball = head;
	if (ate) {
		game->arcade.score += 10;
		impact(game, snake->food);
		snake->body[snake->length++] = tail;
		if (snake->length == SNAKE_SEGMENTS || !snake_food(game)) {
			conclude(game, PAGE_WIN);
		}
	}
}

void arcade_tick(struct game *game, struct point tilt)
{
	if (game->page != PAGE_PLAY || game->mode <= MODE_MAZE || game->mode >= MODE_COUNT) {
		return;
	}

	const float tick = MAZE_TICK_MS / 1000.0f;
	const float step = tick / 4;
	struct point force = add((struct point){0}, tilt, 560);

	game->elapsed += MAZE_TICK_MS;
	switch (game->mode) {
	case MODE_PINBALL: pinball_tick(game, tilt, tick); return;
	case MODE_SPACE: space_tick(game, force, tick); return;
	case MODE_SNAKE: snake_tick(game, tilt, tick); return;
	default: break;
	}
	if (game->mode == MODE_BALANCE) {
		force = add(force, balance_wind(game), 1);
	}
	if (game->mode != MODE_COINS) {
		game->arcade.score = game->elapsed / 100;
	}
	if (game->mode == MODE_SURVIVAL && game->elapsed == 120000) {
		adventure_award(game, ACH_SURVIVOR);
	}
	game->velocity = limit_speed(add((struct point){0}, add(game->velocity, force, tick), 0.952f), 130);
	if (game->mode != MODE_BALANCE) {
		ensure_enemies(game);
	}
	for (unsigned int substep = 0; substep < 4 && game->page == PAGE_PLAY; substep++) {
		game->ball = add(game->ball, game->velocity, step);
		contain(&game->ball, &game->velocity, MAZE_RADIUS - MAZE_BALL, false);
		if (game->mode == MODE_BALANCE) {
			if (!near(game->ball, center, game->arcade.safe_radius - MAZE_BALL)) {
				conclude(game, PAGE_FAIL);
			}
		} else {
			advance_enemies(game, step);
		}
	}
	if (game->mode == MODE_COINS) {
		collect_coins(game);
		if (game->elapsed >= ARCADE_COIN_MS) {
			conclude(game, PAGE_WIN);
		}
	}
}
