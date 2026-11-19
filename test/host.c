#include "art.h"
#include "maze.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static uint16_t panel[MAZE_HEIGHT][MAZE_WIDTH];
static uint16_t reference[MAZE_HEIGHT][MAZE_WIDTH];
static unsigned int written;

static int write_pixels(int x, int y, int width, int height, const uint16_t *pixels)
{
	assert(x >= 0 && y >= 0 && width > 0 && height > 0);
	assert(x + width <= MAZE_WIDTH && y + height <= MAZE_HEIGHT);
	for (int row = 0; row < height; row++) {
		memcpy(&panel[y + row][x], pixels + row * width, (size_t)width * sizeof(*pixels));
	}
	written += width * height;
	return 0;
}

static bool near(struct point a, struct point b, float radius)
{
	return hypotf(a.x - b.x, a.y - b.y) <= radius;
}

static bool test_route(struct game game)
{
	const struct level *level = &game.level;
	const struct level_spec *spec = level->spec;
	static bool seen[4][MAZE_HEIGHT][MAZE_WIDTH];
	static struct { uint8_t x, y, keys; } queue[4 * MAZE_WIDTH * MAZE_HEIGHT];
	static const int moves[][2] = {{2, 0}, {-2, 0}, {0, 2}, {0, -2}};
	unsigned int head = 0, tail = 1;
	bool won = false;
	uint16_t reachable_coins = 0;

	memset(seen, 0, sizeof(seen));
	assert(maze_can_occupy(level, level->start, true));
	assert(maze_can_occupy(level, level->goal, true));
	assert(level->wall_count <= MAZE_MAX_WALLS && level->trap_count <= MAZE_MAX_TRAPS);
	queue[0].x = (short)level->start.x;
	queue[0].y = (short)level->start.y;
	queue[0].keys = 0;
	seen[0][queue[0].y][queue[0].x] = true;
	while (head < tail) {
		int x = queue[head].x, y = queue[head].y;
		struct point current = {x, y};

		game.run.keys = queue[head++].keys;
		game.elapsed = 2100;
		if (near(current, level->goal, 9)) {
			won = true;
			continue;
		}
		for (unsigned int idx = 0; idx < spec->coin_count; idx++) {
			if (!(reachable_coins & MAZE_BIT(idx)) && near(current, spec->coins[idx], 10) &&
			    maze_route_open(&game, current, spec->coins[idx])) {
				reachable_coins |= MAZE_BIT(idx);
			}
		}
		for (unsigned int move = 0; move < 4; move++) {
			struct point next = {x + moves[move][0], y + moves[move][1]};
			uint16_t keys = game.run.keys;

			if (!maze_route_open(&game, current, next)) {
				continue;
			}
			bool unsafe = false;

			for (unsigned int idx = 0; idx < spec->feature_count; idx++) {
				const struct feature *feature = &spec->features[idx];

				if (!maze_feature_on(&game, feature)) {
					continue;
				}
				if (feature->kind == MECH_KEY && !feature->door &&
				    near(next, feature->a, 11)) {
					keys |= MAZE_BIT(feature->id);
				} else if (feature->kind == MECH_PORTAL) {
					if (near(next, feature->a, feature->radius) &&
					    !near(current, feature->a, feature->radius)) {
						next = feature->b;
					} else if (near(next, feature->b, feature->radius) &&
						   !near(current, feature->b, feature->radius)) {
						next = feature->a;
					}
				} else if (feature->kind == MECH_MOVING) {
					for (unsigned int phase = 0; phase <= 12; phase++) {
						struct point center = maze_mover(feature, feature->period * phase / 24.0f);

						unsafe |= near(next, center, feature->radius + MAZE_BALL + 3);
					}
				} else if (feature->kind == MECH_ROTOR) {
					unsafe |= near(next, feature->a, feature->radius + MAZE_BALL + 3);
				}
			}
			int next_x = next.x, next_y = next.y;

			assert(keys < 4 && next_x >= 0 && next_x < MAZE_WIDTH &&
			       next_y >= 0 && next_y < MAZE_HEIGHT);
			if (unsafe || seen[keys][next_y][next_x]) {
				continue;
			}
			seen[keys][next_y][next_x] = true;
			assert(tail < 4 * MAZE_WIDTH * MAZE_HEIGHT);
			queue[tail].x = (short)next_x;
			queue[tail].y = (short)next_y;
			queue[tail++].keys = keys;
		}
	}
	uint16_t expected = MAZE_BIT(spec->coin_count) - 1u;

	printf("%s: %u positions; goal %s; coins %u/%u\n", level->name, head,
	       won ? "OK" : "UNREACHABLE", maze_coin_count(reachable_coins), spec->coin_count);
	fflush(stdout);
	if (reachable_coins != expected) {
		printf("Unreachable coins mask: %x\n", expected & ~reachable_coins);
		fflush(stdout);
	}
	return won && reachable_coins == expected;
}

static void test_physics(void)
{
	struct game game = {.page = PAGE_PLAY};

	maze_load(&game, 0);
	for (int tick = 0; tick < 150; tick++) {
		maze_tick(&game, (struct point){0, 1});
		assert(maze_can_occupy(&game.level, game.ball, true));
	}
	assert(game.ball.y > 90 && game.ball.y <= 91);
	assert(game.run.react_until > game.elapsed);
	struct point at_wall = game.ball;

	for (int tick = 0; tick < 5; tick++) {
		maze_tick(&game, (struct point){1, 1});
	}
	assert(game.ball.x > at_wall.x && game.ball.y <= 91);
	game.page = PAGE_PAUSE;
	uint32_t clock = game.elapsed;

	maze_tick(&game, (struct point){1, 1});
	assert(game.elapsed == clock);
	game.page = PAGE_PLAY;
	game.ball = game.level.goal;
	maze_tick(&game, (struct point){0});
	assert(game.page == PAGE_WIN && game.new_record && game.record[0] == clock + MAZE_TICK_MS);
	maze_load(&game, 0);
	assert(game.record[0] == clock + MAZE_TICK_MS && game.elapsed == 0);
	game.page = PAGE_PLAY;
	game.ball = game.level.goal;
	maze_tick(&game, (struct point){0});
	assert(game.record[0] == MAZE_TICK_MS && game.new_record);
	maze_load(&game, 2);
	game.page = PAGE_PLAY;
	game.ball = game.level.traps[0].center;
	maze_tick(&game, (struct point){0});
	assert(game.page == PAGE_FAIL);
	maze_load(&game, 2);
	game.page = PAGE_PLAY;
	for (unsigned int step = 0; step < 2500 && game.page == PAGE_PLAY; step++) {
		maze_tick(&game, (struct point){cosf(step * 0.03f), sinf(step * 0.03f)});
		assert(maze_can_occupy(&game.level, game.ball, false));
	}
}

static struct game scenario(unsigned int index)
{
	struct game game = {.page = PAGE_PLAY};

	maze_load(&game, index);
	return game;
}

static void test_timed_gate(void)
{
	struct game game = scenario(18);
	const struct feature *gate = game.level.spec->features;
	struct point from = {120, 113}, to = {120, 133};

	assert(gate->kind == MECH_TIMED && maze_gate_active(&game, gate));
	assert(!maze_route_open(&game, from, to));
	game.ball = from;
	game.velocity.y = 110;
	for (unsigned int tick = 0; tick < 15; tick++) {
		maze_tick(&game, (struct point){0, 1});
	}
	assert(game.ball.y <= 116 && game.run.contacts > 0);
	game.elapsed = 2100;
	assert(!maze_gate_active(&game, gate) && maze_route_open(&game, from, to));
	for (unsigned int tick = 0; tick < 35; tick++) {
		maze_tick(&game, (struct point){0, 1});
	}
	assert(game.ball.y > to.y);
	game.ball = (struct point){120, 123};
	game.elapsed = gate->period;
	assert(!maze_gate_active(&game, gate));
	assert(maze_route_open(&game, game.ball, to));
}

static void test_adventure(void)
{
	struct game game = scenario(13);
	uint32_t all = 0;

	assert(adventure_resume(&game) == 0);
	assert(!adventure_buy(&game, SKIN_NEON) && !adventure_equip(&game, SKIN_ICE));
	game.elapsed = 1000;
	assert(adventure_stars(&game) == 1);
	game.run.coins = game.run.spawned;
	game.run.contacts = 1;
	assert(adventure_stars(&game) == 2);
	game.run.contacts = 0;
	assert(adventure_stars(&game) == 3);
	game.elapsed = game.level.spec->target_seconds * 1000u + 1;
	assert(adventure_stars(&game) == 1);

	for (unsigned int position = 0; position < MAZE_LEVELS; position++) {
		unsigned int index = adventure_level(position);

		assert(adventure_position(index) == position && !(all & MAZE_BIT(index)));
		assert(adventure_resume(&game) == index);
		all |= MAZE_BIT(index);
		maze_load(&game, index);
		game.elapsed = 1000;
		game.run.coins = game.run.spawned;
		game.run.score = 200;
		game.run.max_combo = maze_coin_count(game.run.coins);
		adventure_complete(&game);
		assert(game.new_record && game.record[index] == 1000 && game.high_score[index] == 200);
		assert(game.stars == 3 && game.growth.stars[index] == 3);
	}
	assert(adventure_resume(&game) == 0);
	assert(game.growth.skins == (MAZE_BIT(SKIN_ICE) | MAZE_BIT(SKIN_LAVA) | MAZE_BIT(SKIN_STAR)));
	assert(game.growth.achievements == (MAZE_BIT(ACH_COUNT) - 1u) -
	       MAZE_BIT(ACH_SURVIVOR) - MAZE_BIT(ACH_COLLECTOR));
	uint32_t wallet = game.wallet;

	assert(adventure_buy(&game, SKIN_NEON) && adventure_buy(&game, SKIN_PIXEL));
	assert(!adventure_buy(&game, SKIN_NEON) && game.wallet == wallet - 65);
	assert(adventure_equip(&game, SKIN_PIXEL) && game.growth.skin == SKIN_PIXEL);
	assert(game.growth.achievements & MAZE_BIT(ACH_COLLECTOR));
	arcade_start(&game, MODE_SURVIVAL);
	game.elapsed = 120000 - MAZE_TICK_MS;
	arcade_tick(&game, (struct point){0});
	assert(game.growth.achievements == MAZE_BIT(ACH_COUNT) - 1u);
	puts("Campaign completion, star thresholds, six skins and eight achievements: OK");
}

static void test_challenges(void)
{
	struct game game = {0}, replay = {0};
	uint32_t layouts = 0;
	unsigned int mirrored = 0, generated_traps = 0;

	for (uint32_t seed = 1; seed <= 96; seed++) {
		unsigned int tier = seed % (CHALLENGE_MAX_TIER + 1u);

		challenge_load(&game, EXP_RANDOM, seed, tier);
		assert(game.challenge_seed == seed && game.challenge_tier == tier && challenge_valid(&game));
		challenge_load(&replay, EXP_RANDOM, seed, tier);
		assert(game.index == replay.index && game.level.mirrored == replay.level.mirrored);
		assert(game.run.spawned == replay.run.spawned && game.level.trap_count == replay.level.trap_count);
		assert(memcmp(game.level.traps, replay.level.traps,
			      game.level.trap_count * sizeof(*game.level.traps)) == 0);
		layouts |= MAZE_BIT(game.index);
		mirrored += game.level.mirrored;
		generated_traps += game.level.trap_count > game.level.spec->trap_count;
	}
	assert(mirrored > 10 && generated_traps > 10 && (layouts & MAZE_BIT(24)));
	challenge_load(&replay, EXP_RANDOM, 1000, 50);
	replay.elapsed = 5000;
	adventure_complete(&replay);
	assert(replay.challenge_tier == CHALLENGE_MAX_TIER && replay.run.score == 250);
	assert(!replay.growth.stars[replay.index] && !replay.record[replay.index]);
	maze_load(&replay, 0);
	assert(challenge_valid(&replay));
	replay.level.traps[0] = (struct trap){replay.level.start, 7};
	replay.level.trap_count = 1;
	assert(!challenge_valid(&replay));

	const uint32_t date = 20240229;

	assert(adventure_date_valid(date) && !adventure_date_valid(20230229));
	assert(adventure_date_adjust(date, 0, -1) == 20230228);
	assert(adventure_date_adjust(20240131, 1, 1) == date);
	assert(adventure_date_adjust(20240101, 2, -1) == 20240131);
	challenge_load(&game, EXP_DAILY, date, 3 + date % 4);
	challenge_load(&replay, EXP_DAILY, date, 3 + date % 4);
	assert(game.index == replay.index && game.level.mirrored == replay.level.mirrored);
	game.elapsed = 20000;
	adventure_complete(&game);
	assert(game.new_record && adventure_daily_best(&game, date) == 20000);
	game.elapsed = 25000;
	adventure_complete(&game);
	assert(!game.new_record && adventure_daily_best(&game, date) == 20000);
	game.elapsed = 18000;
	adventure_complete(&game);
	assert(game.new_record && adventure_daily_best(&game, date) == 18000);
	assert(!game.growth.stars[game.index] && !game.record[game.index] && !adventure_daily_best(&game, 20240301));
	puts("96 seeded layouts, deterministic replay and per-date daily records: OK");
}

static void place(struct game *game, struct point at, struct point tilt)
{
	game->ball = at;
	game->velocity = (struct point){0};
	maze_tick(game, tilt);
}

static void test_features(void)
{
	struct game game = scenario(3);
	const struct feature *patrol = game.level.spec->features;

	assert(!near(maze_mover(patrol, 0), maze_mover(patrol, patrol->period / 2), 10));
	place(&game, game.level.spec->items[0].at, (struct point){0});
	assert(game.run.shield);
	place(&game, maze_mover(patrol, game.elapsed), (struct point){0});
	assert(!game.run.shield && game.page == PAGE_PLAY);
	for (unsigned int tick = 0; tick < 40; tick++) {
		place(&game, maze_mover(patrol, game.elapsed), (struct point){0});
	}
	assert(game.page == PAGE_PLAY);
	place(&game, game.level.start, (struct point){0});
	place(&game, maze_mover(patrol, game.elapsed), (struct point){0});
	assert(game.page == PAGE_FAIL);
	game = scenario(3);
	game.enabled &= ~MAZE_BIT(MECH_MOVING);
	place(&game, patrol->a, (struct point){0});
	assert(game.page == PAGE_PLAY);

	game = scenario(4);
	const struct feature *portal = game.level.spec->features;

	place(&game, portal->a, (struct point){0});
	assert(near(game.ball, portal->b, 0.1f));
	maze_tick(&game, (struct point){0});
	assert(near(game.ball, portal->b, 0.1f));
	place(&game, (struct point){77, 159}, (struct point){0});
	place(&game, portal->b, (struct point){0});
	assert(near(game.ball, portal->a, 0.1f));
	game = scenario(4);
	game.enabled &= ~MAZE_BIT(MECH_PORTAL);
	place(&game, portal->a, (struct point){0});
	assert(near(game.ball, portal->a, 0.1f));

	game = scenario(5);
	struct game normal = game;

	normal.enabled &= ~MAZE_BIT(MECH_ICE);
	game.ball = normal.ball = (struct point){139, 115};
	game.velocity = normal.velocity = (struct point){20, 0};
	maze_tick(&game, (struct point){0});
	maze_tick(&normal, (struct point){0});
	assert(game.velocity.x > normal.velocity.x);
	place(&game, game.level.spec->items[0].at, (struct point){0});
	assert(game.run.slow_until - game.elapsed == 5000);
	game.velocity.x = 109;
	maze_tick(&game, (struct point){1, 0});
	assert(hypotf(game.velocity.x, game.velocity.y) <= 58.01f);
	game.elapsed = game.run.slow_until;
	maze_tick(&game, (struct point){1, 0});
	assert(game.velocity.x > 58);

	game = scenario(6);
	const struct feature *arm = game.level.spec->features;

	game.ball = (struct point){arm->a.x + 12, arm->a.y - 9};
	game.velocity = (struct point){0, 74};
	maze_tick(&game, (struct point){0});
	assert(game.velocity.y < 0 && game.run.contacts);
	assert(maze_can_occupy(&game.level, game.ball, false));
	place(&game, game.level.spec->items[0].at, (struct point){0});
	assert(game.run.magnet_until - game.elapsed == 5000);
	place(&game, (struct point){172, 124}, (struct point){0});
	assert(game.run.coins & MAZE_BIT(2));

	game = scenario(7);
	struct point top = {125, 117}, bottom = {125, 148};

	assert(!maze_route_open(&game, top, bottom));
	game.run.keys = MAZE_BIT(1);
	assert(!maze_route_open(&game, top, bottom));
	place(&game, game.level.spec->features[0].a, (struct point){0});
	assert(maze_has_key(&game, 0) && maze_route_open(&game, top, bottom));
	const struct item *blink = game.level.spec->items;

	place(&game, blink->at, (struct point){0});
	assert(near(game.ball, blink->target, 0.1f) && game.run.items);
	struct item invalid = *blink;
	struct level_spec spec = *game.level.spec;

	invalid.target = bottom;
	spec.items = &invalid;
	game = scenario(7);
	game.level.spec = &spec;
	place(&game, invalid.at, (struct point){0});
	assert(!game.run.items && near(game.ball, invalid.at, 0.1f));

	game = scenario(8);
	normal = game;
	normal.enabled &= ~MAZE_BIT(MECH_LOW_G);
	place(&game, (struct point){157, 106}, (struct point){1, 0});
	place(&normal, (struct point){157, 106}, (struct point){1, 0});
	assert(game.velocity.x > 0 && game.velocity.x < normal.velocity.x * 0.5f);
	game = scenario(9);
	place(&game, (struct point){131, 107}, (struct point){0});
	assert(game.velocity.x > 0);
	game = scenario(10);
	top = (struct point){140, 101};
	bottom = (struct point){140, 138};
	assert(maze_route_open(&game, top, bottom) && !maze_route_open(&game, bottom, top));
	place(&game, top, (struct point){0});
	for (unsigned int tick = 0; tick < 30; tick++) {
		maze_tick(&game, (struct point){0, 1});
	}
	assert(game.ball.y > 126);
	place(&game, bottom, (struct point){0});
	for (unsigned int tick = 0; tick < 35; tick++) {
		maze_tick(&game, (struct point){0, -1});
	}
	assert(game.ball.y >= 126);
	game = scenario(11);
	assert(maze_visible(&game, game.ball) && !maze_visible(&game, game.level.goal));
	assert(maze_coin_count(game.run.spawned) == 8);
	game.enabled &= ~MAZE_BIT(MECH_DARK);
	assert(maze_visible(&game, game.level.goal));
	game = scenario(12);
	place(&game, (struct point){147, 104}, (struct point){1, 0});
	assert(game.velocity.x < 0);
	puts("All 10 mechanisms and four items: OK");
}

static void test_coins(void)
{
	struct game game = scenario(3);
	const struct level_spec *spec = game.level.spec;

	game.enabled &= ~MAZE_BIT(MECH_MOVING);
	for (unsigned int idx = 0; idx < 5; idx++) {
		place(&game, spec->coins[idx], (struct point){0});
	}
	assert(game.run.combo == 5 && game.run.score == 70 && game.run.gain == 20);
	game.elapsed += 3020;
	place(&game, spec->coins[5], (struct point){0});
	assert(game.run.combo == 1 && game.run.max_combo == 5 && game.run.score == 80);
	place(&game, spec->coins[6], (struct point){0});
	place(&game, spec->goal, (struct point){0});
	assert(game.page == PAGE_WIN && game.run.score == 340 && game.wallet == 7);
	maze_load(&game, 3);
	assert(game.wallet == 7 && game.high_score[3] == 340 && !game.run.coins && !game.run.shield);
}

static void test_renderer(void)
{
	struct game game = {.page = PAGE_PLAY};
	struct art_state partial = {0};

	maze_load(&game, 2);
	assert(art_present(&partial, &game, 0, 0, write_pixels) == 0);
	assert(written == MAZE_WIDTH * MAZE_HEIGHT);
	for (unsigned int step = 0; step < 25; step++) {
		struct art_state full = {0};
		unsigned int progress = step < 21 ? step : 0;

		game.ball.x += 2;
		game.ball.y += 3;
		game.elapsed += 100;
		written = 0;
		assert(art_present(&partial, &game, progress, 0, write_pixels) == 0);
		assert(written > 0 && written < (MAZE_WIDTH * MAZE_HEIGHT) / 8);
		memcpy(reference, panel, sizeof(reference));
		assert(art_present(&full, &game, progress, 0, write_pixels) == 0);
		assert(memcmp(panel, reference, sizeof(panel)) == 0);
	}
}

static void test_mascot_renderer(void)
{
	struct game game = scenario(7);
	struct art_state partial = {0}, full = {0};

	assert(art_present(&partial, &game, 0, 0, write_pixels) == 0);
	memcpy(reference, panel, sizeof(reference));
	game.run.react_until = 300;
	written = 0;
	assert(art_present(&partial, &game, 0, 0, write_pixels) == 0);
	assert(written > 0 && memcmp(panel, reference, sizeof(panel)) != 0);
	game.elapsed = game.run.react_until;
	assert(art_present(&partial, &game, 0, 0, write_pixels) == 0);
	memcpy(reference, panel, sizeof(reference));
	assert(art_present(&full, &game, 0, 0, write_pixels) == 0);
	assert(memcmp(panel, reference, sizeof(panel)) == 0);

	game.stars = 3;
	for (enum page page = PAGE_WIN; page <= PAGE_FAIL; page++) {
		game.page = page;
		assert(art_present(&partial, &game, 0, 0, write_pixels) == 0);
		for (unsigned int pulse = 1; pulse <= 9; pulse++) {
			unsigned int frame = pulse % 9;

			assert(art_present(&partial, &game, 0, frame, write_pixels) == 0);
			memcpy(reference, panel, sizeof(reference));
			full = (struct art_state){0};
			assert(art_present(&full, &game, 0, frame, write_pixels) == 0);
			assert(memcmp(panel, reference, sizeof(panel)) == 0);
		}
	}
	game.page = PAGE_COUNTDOWN;
	game.countdown = 3;
	assert(art_present(&partial, &game, 0, 0, write_pixels) == 0);
	game.countdown = 2;
	assert(art_present(&partial, &game, 0, 0, write_pixels) == 0);
	memcpy(reference, panel, sizeof(reference));
	full = (struct art_state){0};
	assert(art_present(&full, &game, 0, 0, write_pixels) == 0);
	assert(memcmp(panel, reference, sizeof(panel)) == 0);
}

static void test_dynamic_renderer(void)
{
	for (unsigned int idx = 3; idx <= MAZE_LEVELS; idx++) {
		struct art_state partial = {0};
		struct game game = scenario(idx);
		game.chinese = idx % 2 == 0;
		if (idx == 18 || idx == 20) {
			game.elapsed = 1500;
		} else if (idx == MAZE_LEVELS) {
			for (uint32_t seed = 1; seed <= 16; seed++) {
				challenge_load(&game, EXP_RANDOM, seed, CHALLENGE_MAX_TIER);
				if (game.level.mirrored && game.level.spec->coin_count) {
					break;
				}
			}
			assert(game.level.mirrored && game.level.spec->coin_count);
		}

		assert(art_present(&partial, &game, 0, 0, write_pixels) == 0);
		for (unsigned int frame = 0; frame < 24; frame++) {
			struct art_state full = {0};
			struct run *run = &game.run;

			game.elapsed += 40;
			game.ball.x += 1;
			game.ball.y += 2;
			if (frame == 2) {
				run->flash = maze_transform(&game.level, game.level.spec->coins[0]);
				run->flash_until = game.elapsed + 420;
				run->coins = run->spawned & (uint16_t)(-run->spawned);
				run->items = 1;
				run->keys = 3;
				run->gain = 10;
				run->combo = 1;
				run->score = 10;
				run->shield = true;
				run->slow_until = game.elapsed + 640;
				run->magnet_until = game.elapsed + 460;
			}
			if (frame == 5) {
				run->shield = false;
			}
			if (frame == 8) {
				game.ball = (struct point){161, 181};
			}
			unsigned int progress = frame == 16 ? 8 : 0;

			written = 0;
			assert(art_present(&partial, &game, progress, 0, write_pixels) == 0);
			assert(written > 0 && written < (MAZE_WIDTH * MAZE_HEIGHT) / 2);
			memcpy(reference, panel, sizeof(reference));
			assert(art_present(&full, &game, progress, 0, write_pixels) == 0);
			if (memcmp(panel, reference, sizeof(panel)) != 0) {
				for (int row = 0; row < MAZE_HEIGHT; row++) {
					for (int col = 0; col < MAZE_WIDTH; col++) {
						if (panel[row][col] != reference[row][col]) {
							printf("Dirty mismatch L%u frame %u at %d,%d\n",
							       idx + 1, frame, col, row);
							fflush(stdout);
							assert(false);
						}
					}
				}
			}
		}
	}
}

static void test_home_controls(void)
{
	struct game game = scenario(7);
	const int confirm_y = SECONDARY_Y + ACTION_HEIGHT / 2;
	const int cancel_x = SPLIT_RIGHT + SPLIT_W / 2;
	const int quit_x = SPLIT_X + SPLIT_W / 2;
	const int primary_y = PRIMARY_Y + ACTION_HEIGHT / 2;
	static const enum page home_pages[] = {
		PAGE_GAMES, PAGE_WORLDS, PAGE_SETTINGS, PAGE_EXTRAS, PAGE_CHALLENGES,
		PAGE_ARCADE, PAGE_SELECT, PAGE_COUNTDOWN, PAGE_BRIEF, PAGE_PAUSE,
		PAGE_WIN, PAGE_FAIL, PAGE_IMU_ERROR,
	};

	assert(art_action_at(&game, PAUSE_X + 2, PAUSE_Y + 2) == ACTION_PAUSE);
	assert(art_action_at(&game, 55, 54) == ACTION_NONE);
	for (unsigned int i = 0; i < sizeof(home_pages) / sizeof(home_pages[0]); i++) {
		game.page = home_pages[i];
		assert(art_action_at(&game, 55, 54) == ACTION_HOME);
		assert(art_action_at(&game, 33, 54) == ACTION_HOME);
		assert(art_action_at(&game, 76, 54) == ACTION_HOME);
		assert(art_action_at(&game, 55, 75) == ACTION_HOME);
		assert(art_action_at(&game, 77, 54) == ACTION_NONE);
		assert(art_action_at(&game, 55, 76) == ACTION_NONE);
	}
	game.page = PAGE_PAUSE;
	assert(art_action_at(&game, 120, primary_y) == ACTION_PRIMARY);
	assert(art_action_at(&game, 120, confirm_y) == ACTION_REPLAY);
	game.page = PAGE_QUIT;
	assert(art_action_at(&game, quit_x, confirm_y) == ACTION_HOME);
	assert(art_action_at(&game, cancel_x, confirm_y) == ACTION_PRIMARY);
	assert(art_action_at(&game, 120, confirm_y) == ACTION_NONE);
	assert(art_action_at(&game, 55, 54) == ACTION_NONE);
	game.page = PAGE_WIN;
	assert(art_action_at(&game, 120, primary_y) == ACTION_PRIMARY);
	assert(art_action_at(&game, 120, confirm_y) == ACTION_REPLAY);
	game.index = MAZE_LEVELS - 1;
	assert(art_action_at(&game, 120, primary_y) == ACTION_WORLDS);
	assert(art_action_at(&game, 120, confirm_y) == ACTION_REPLAY);
	game.page = PAGE_BRIEF;
	assert(art_action_at(&game, 120, FOOTER_Y + 8) == ACTION_NONE);
	game.page = PAGE_SELECT;
	for (unsigned int index = 0; index < MAZE_LEVELS; index++) {
		game.index = index;
		assert(art_action_at(&game, 120, 106) == ACTION_WORLDS);
		assert(art_action_at(&game, 55, 92) == ACTION_NONE);
		assert(art_action_at(&game, 65, 190) == ACTION_PREV);
		assert(art_action_at(&game, 175, 190) == ACTION_NEXT);
		assert(art_action_at(&game, 120, 190) == ACTION_PRIMARY);
	}
	game.page = PAGE_WORLDS;
	assert(art_action_at(&game, 69, 94) == ACTION_WORLD_1);
	assert(art_action_at(&game, 170, 94) == ACTION_WORLD_2);
	assert(art_action_at(&game, 69, 132) == ACTION_WORLD_3);
	assert(art_action_at(&game, 170, 132) == ACTION_WORLD_4);
	assert(art_action_at(&game, 120, 169) == ACTION_WORLD_5);
	assert(art_action_at(&game, 120, 206) == ACTION_LEVELS);
	game.page = PAGE_SETTINGS;
	assert(art_action_at(&game, 60, 91) == ACTION_LANGUAGE);
	assert(art_action_at(&game, 120, 114) == ACTION_BRIGHTNESS);
	assert(art_action_at(&game, 120, 138) == ACTION_SENSITIVITY);
	assert(art_action_at(&game, 120, 162) == ACTION_HAND);
	assert(art_action_at(&game, 120, 186) == ACTION_ROTATE);
	assert(art_action_at(&game, 120, 210) == ACTION_QUIET);
	assert(art_action_at(&game, 120, 222) == ACTION_NONE);
	game.page = PAGE_SKINS;
	assert(art_action_at(&game, 55, 54) == ACTION_TERTIARY);
	assert(art_action_at(&game, 76, 75) == ACTION_TERTIARY);
	assert(art_action_at(&game, 55, 76) == ACTION_NONE);
	assert(art_action_at(&game, 120, FOOTER_Y + 8) == ACTION_NONE);
	assert(art_action_at(&game, 120, primary_y) == ACTION_PRIMARY);
	assert(art_action_at(&game, quit_x, confirm_y) == ACTION_PREV);
	assert(art_action_at(&game, cancel_x, confirm_y) == ACTION_NEXT);
	game.page = PAGE_MENU;
	assert(art_action_at(&game, 120, 138) == ACTION_PRIMARY);
	assert(art_action_at(&game, 120, 176) == ACTION_SKINS);
	assert(art_action_at(&game, 120, 210) == ACTION_SETTINGS);
	assert(art_action_at(&game, 120, 82) == ACTION_NONE);
	game.page = PAGE_GAMES;
	assert(art_action_at(&game, 120, 91) == ACTION_PRIMARY);
	assert(art_action_at(&game, 75, 124) == ACTION_COIN_GAME);
	assert(art_action_at(&game, 165, 124) == ACTION_DODGE_GAME);
	assert(art_action_at(&game, 75, 156) == ACTION_BALANCE_GAME);
	assert(art_action_at(&game, 165, 156) == ACTION_PINBALL_GAME);
	assert(art_action_at(&game, 75, 188) == ACTION_SPACE_GAME);
	assert(art_action_at(&game, 165, 188) == ACTION_SNAKE_GAME);
	assert(art_action_at(&game, 120, 218) == ACTION_EXTRAS);
	game.page = PAGE_FAIL;
	assert(art_action_at(&game, 120, confirm_y) == ACTION_NONE);
	game.page = PAGE_CALIBRATE;
	assert(art_action_at(&game, 120, confirm_y) == ACTION_NONE);

	game.page = PAGE_QUIT;
	game.run.coins = 0x7;
	game.run.score = 40;
	game.run.shield = true;
	game.elapsed = 3240;
	game.record[0] = 2830;
	game.high_score[0] = 120;
	game.wallet = MAZE_SKIN_UNLOCK;
	game.growth.skin = SKIN_ICE;
	maze_tick(&game, (struct point){1, 1});
	assert(game.elapsed == 3240 && game.run.score == 40);
	maze_leave(&game);
	assert(game.page == PAGE_MENU && game.elapsed == 0 && game.run.coins == 0 &&
	       !game.run.shield && game.run.score == 0);
	assert(game.record[0] == 2830 && game.high_score[0] == 120 &&
	       game.wallet == MAZE_SKIN_UNLOCK && game.growth.skin == SKIN_ICE);
	puts("Pause, confirm quit, and home controls: OK");
}

static void test_arcade(void)
{
	struct game game = scenario(7);
	const struct point center = {MAZE_CENTER_X, MAZE_CENTER_Y};

	game.wallet = 14;
	game.record[7] = 22000;
	game.run.shield = true;
	game.selected_mode = MODE_COINS;
	game.page = PAGE_GAMES;
	assert(art_action_at(&game, 75, 124) == ACTION_COIN_GAME);
	game.page = PAGE_ARCADE;
	assert(art_action_at(&game, 120, PRIMARY_Y + 8) == ACTION_PRIMARY);
	assert(art_action_at(&game, 55, 54) == ACTION_HOME);
	arcade_start(&game, MODE_COINS);
	assert(game.mode == MODE_COINS && game.elapsed == 0 && !game.run.shield &&
	       game.wallet == 14 && game.record[7] == 22000);
	assert(game.arcade.enemies_active == 1);
	game.arcade.immune_until = ARCADE_COIN_MS + 1000;
	for (unsigned int slot = 0; slot < ARCADE_COIN_SLOTS; slot++) {
		game.arcade.coins[slot] = (struct arcade_coin){.at = center, .bonus = slot == 4};
	}
	arcade_tick(&game, (struct point){0});
	assert(game.arcade.collected == 5 && game.arcade.peak == 5 && game.arcade.score == 110);
	assert(game.arcade.coins[0].respawn > game.elapsed);
	const uint32_t deadline = game.arcade.coins[0].respawn;
	game.page = PAGE_PAUSE;
	arcade_tick(&game, (struct point){1, 1});
	assert(game.elapsed == MAZE_TICK_MS && game.arcade.coins[0].respawn == deadline);
	game.page = PAGE_PLAY;
	while (game.elapsed < deadline) {
		arcade_tick(&game, (struct point){0});
	}
	assert(game.arcade.coins[0].respawn != deadline);
	game.elapsed = 15000;
	arcade_tick(&game, (struct point){0});
	assert(game.arcade.combo == 0 && game.arcade.enemies_active == 2);
	game.elapsed = ARCADE_COIN_MS - MAZE_TICK_MS;
	arcade_tick(&game, (struct point){0});
	assert(game.page == PAGE_WIN && game.elapsed == ARCADE_COIN_MS);
	assert(game.arcade_best[MODE_COINS] == 110 && game.new_record);
	assert(art_action_at(&game, 120, PRIMARY_Y + 8) == ACTION_REPLAY);

	arcade_start(&game, MODE_SURVIVAL);
	assert(game.elapsed == 0 && game.arcade.enemies_active == 2 && game.arcade.lives == 3 &&
	       !game.arcade.immune_until && !game.arcade.collected && game.arcade_best[MODE_COINS] == 110);
	assert(game.arcade.enemies[0].kind == ENEMY_LINEAR && game.arcade.enemies[1].kind == ENEMY_BOUNCE);
	game.arcade.enemies[0].at = center;
	arcade_tick(&game, (struct point){0});
	assert(game.arcade.lives == 2 && game.arcade.immune_until == 1020);
	game.arcade.enemies[0].at = center;
	arcade_tick(&game, (struct point){0});
	assert(game.arcade.lives == 2);
	for (unsigned int hit = 0; hit < 2; hit++) {
		game.elapsed = game.arcade.immune_until;
		game.arcade.enemies[0].at = center;
		arcade_tick(&game, (struct point){0});
	}
	assert(game.page == PAGE_FAIL && game.arcade.lives == 0 && game.arcade_best[MODE_SURVIVAL] > 0);
	assert(art_action_at(&game, 55, 54) == ACTION_HOME);
	arcade_start(&game, MODE_SURVIVAL);
	game.arcade.immune_until = 999999;
	game.elapsed = 220000;
	arcade_tick(&game, (struct point){0});
	assert(game.arcade.enemies_active == ARCADE_ENEMY_SLOTS &&
	       game.arcade.enemies[2].kind == ENEMY_CHASE);
	game.arcade.enemies[1].at = (struct point){214.9f, 128};
	game.arcade.enemies[1].velocity = (struct point){1, 0};
	game.arcade.enemies[2].at = (struct point){150, 128};
	game.arcade.enemies[2].velocity = (struct point){0, 1};
	arcade_tick(&game, (struct point){0});
	assert(game.arcade.enemies[1].velocity.x < 0 && game.arcade.enemies[2].velocity.x < 0);

	arcade_start(&game, MODE_BALANCE);
	assert(game.elapsed == 0 && game.arcade.enemies_active == 0 && game.arcade.safe_radius == 49);
	game.elapsed = 1980;
	arcade_tick(&game, (struct point){0});
	assert((game.arcade.wind.x || game.arcade.wind.y) && near(game.ball, center, 0.01f));
	game.elapsed = 2380;
	arcade_tick(&game, (struct point){0});
	assert(hypotf(game.velocity.x, game.velocity.y) > 1);
	game.elapsed = 10000;
	arcade_tick(&game, (struct point){0});
	assert(game.arcade.safe_radius == 45);
	game.ball.x = 165;
	arcade_tick(&game, (struct point){0});
	assert(game.page == PAGE_FAIL && game.arcade_best[MODE_BALANCE] > 0);
	maze_leave(&game);
	assert(game.page == PAGE_MENU && game.mode == MODE_MAZE && game.elapsed == 0 &&
	       game.arcade.safe_radius == 0 && game.selected_mode == MODE_COINS);
	maze_load(&game, 7);
	assert(game.run.spawned && !game.run.coins && game.arcade_best[MODE_COINS] == 110);
	puts("Arcade timers, coin respawn, three enemy behaviors, balance and reset: OK");
}

static void test_arcade_renderer(void)
{
	for (enum mode mode = MODE_COINS; mode <= MODE_BALANCE; mode++) {
		struct game game = {0};
		struct art_state partial = {0};

		arcade_start(&game, mode);
		game.chinese = true;
		game.elapsed = mode == MODE_BALANCE ? 9800 : 12400;
		game.arcade.immune_until = 13000;
		assert(art_present(&partial, &game, 0, 0, write_pixels) == 0);
		for (unsigned int frame = 0; frame < 32; frame++) {
			struct art_state full = {0};
			unsigned int progress = frame == 18 ? 8 : 0;

			if (mode == MODE_COINS && frame == 4) {
				game.arcade.coins[0].at = game.ball;
			}
			arcade_tick(&game, (struct point){0.09f, -0.04f});
			arcade_tick(&game, (struct point){0.09f, -0.04f});
			assert(game.page == PAGE_PLAY);
			written = 0;
			assert(art_present(&partial, &game, progress, 0, write_pixels) == 0);
			assert(written < MAZE_WIDTH * MAZE_HEIGHT / 2);
			memcpy(reference, panel, sizeof(reference));
			assert(art_present(&full, &game, progress, 0, write_pixels) == 0);
			if (memcmp(panel, reference, sizeof(panel)) != 0) {
				printf("Arcade dirty mismatch: mode %d frame %u\n", mode, frame);
				fflush(stdout);
				assert(false);
			}
		}
	}
}

static void test_motion_games(void)
{
	struct game game = {0};
	struct point no_tilt = {0};

	arcade_start(&game, MODE_PINBALL);
	assert(game.arcade.pinball.loaded && game.arcade.lives == 3);
	assert(art_action_at(&game, 120, 159) == ACTION_TRIGGER);
	assert(art_action_at(&game, PAUSE_X + 3, PAUSE_Y + 15) == ACTION_PAUSE);
	game.settings.left_hand = true;
	assert(art_action_at(&game, 54, 45) == ACTION_PAUSE);
	assert(art_action_at(&game, PAUSE_X + 3, PAUSE_Y + 15) == ACTION_TRIGGER);
	arcade_trigger(&game);
	assert(!game.arcade.pinball.loaded && game.velocity.y < -200);
	for (unsigned int hit = 1; hit <= 2; hit++) {
		game.ball = (struct point){86, 123};
		game.velocity = (struct point){0, -200};
		arcade_tick(&game, no_tilt);
		assert(game.arcade.combo == hit && game.velocity.y > 0);
	}
	assert(game.arcade.score == 150);
	game.arcade.pinball.flip_until = 0;
	game.ball = (struct point){111, 195};
	game.velocity.y = 100;
	arcade_trigger(&game);
	arcade_tick(&game, no_tilt);
	assert(game.velocity.y < 0 && game.arcade.lives == 3);
	for (unsigned int remaining = 3; remaining; remaining--) {
		game.arcade.pinball.loaded = false;
		game.arcade.pinball.flip_until = 0;
		game.ball = (struct point){120, 211};
		game.velocity = (struct point){0, 120};
		arcade_tick(&game, no_tilt);
		assert(game.arcade.lives == remaining - 1);
	}
	assert(game.page == PAGE_FAIL && game.arcade_best[MODE_PINBALL] == 150);

	arcade_start(&game, MODE_SPACE);
	struct space_run *space = &game.arcade.space;
	struct point previous = game.ball;
	arcade_tick(&game, (struct point){1, 0});
	assert(game.ball.x > previous.x);
	game.ball = previous;
	game.velocity = no_tilt;
	space->rocks[0] = (struct space_object){.at = {120, 151}, .radius = 8};
	arcade_trigger(&game);
	arcade_trigger(&game);
	assert(space->shots[0].radius && !space->shots[1].radius);
	arcade_tick(&game, no_tilt);
	assert(!space->rocks[0].radius && game.arcade.score == 75 && game.arcade.collected == 1);
	space->pickup = (struct space_object){.at = game.ball, .radius = 5};
	space->pickup_shield = true;
	arcade_tick(&game, no_tilt);
	assert(space->shield && !space->pickup.radius);
	for (unsigned int remaining = 4; remaining; remaining--) {
		game.arcade.immune_until = 0;
		space->rocks[0] = (struct space_object){.at = game.ball, .radius = 8};
		arcade_tick(&game, no_tilt);
		assert(!space->shield && game.arcade.lives == remaining - 1);
	}
	assert(game.page == PAGE_FAIL && game.arcade_best[MODE_SPACE] == 75);

	arcade_start(&game, MODE_SNAKE);
	struct snake_run *snake = &game.arcade.snake;
	assert(snake->length == 4 && !snake->free_turn);
	snake->food = (struct point){125, 123};
	game.elapsed = snake->step_at - MAZE_TICK_MS;
	arcade_tick(&game, (struct point){-1, 0});
	assert(snake->length == 5 && game.ball.x == 125 && game.arcade.score == 10);
	game.ball.x = SNAKE_RIGHT;
	game.elapsed = snake->step_at - MAZE_TICK_MS;
	arcade_tick(&game, no_tilt);
	assert(game.page == PAGE_FAIL && game.snake_best_length[0] == 5 && arcade_best_score(&game, MODE_SNAKE) == 10);
	game.settings.snake_free = true;
	arcade_start(&game, MODE_SNAKE);
	assert(snake->free_turn);
	previous = game.ball;
	snake->food = (struct point){120, 126};
	arcade_tick(&game, (struct point){0, 1});
	assert(game.ball.x > previous.x && game.ball.y > previous.y && snake->heading.y < 0.1f);
	assert(snake->length == 5 && !arcade_best_score(&game, MODE_SNAKE));
	game.ball.x = SNAKE_RIGHT;
	arcade_tick(&game, no_tilt);
	assert(game.page == PAGE_FAIL && game.snake_best_length[1] == 5 && arcade_best_score(&game, MODE_SNAKE) == 10);
	puts("Pinball launch, flippers and scoring, space shots and shields, both snake styles: OK");
}

static void test_motion_renderer(void)
{
	for (enum mode mode = MODE_PINBALL; mode < MODE_COUNT; mode++) {
		struct game game = {.settings.rotated = true, .settings.snake_free = true};
		struct art_state partial = {0};

		arcade_start(&game, mode);
		game.chinese = true;
		assert(art_present(&partial, &game, 0, 0, write_pixels) == 0);
		for (unsigned int tick = 0; tick < 72; tick++) {
			struct art_state full = {0};
			if (tick % 8 == 0) {
				arcade_trigger(&game);
			}
			arcade_tick(&game, (struct point){cosf(tick / 26.0f), sinf(tick / 26.0f)});
			unsigned int progress = tick == 25 ? 6 : 0;
			unsigned int pulse = tick % 9;

			assert(art_present(&partial, &game, progress, pulse, write_pixels) == 0);
			memcpy(reference, panel, sizeof(reference));
			assert(art_present(&full, &game, progress, pulse, write_pixels) == 0);
			if (memcmp(reference, panel, sizeof(reference)) != 0) {
				printf("Motion rendering mismatch mode %u tick %u\n", mode, tick);
				fflush(stdout);
				assert(false);
			}
		}
	}
	struct game game = {0};
	struct art_state art = {0};

	assert(art_present(&art, &game, 0, 0, write_pixels) == 0);
	memcpy(reference, panel, sizeof(reference));
	game.settings.rotated = true;
	assert(art_present(&art, &game, 0, 0, write_pixels) == 0);
	for (int y = 0; y < MAZE_HEIGHT; y++) {
		for (int x = 0; x < MAZE_WIDTH; x++) {
			assert(panel[y][x] == reference[MAZE_HEIGHT - 1 - y][MAZE_WIDTH - 1 - x]);
		}
	}
}

static void test_language(void)
{
	struct game game = {.page = PAGE_SETTINGS};
	struct art_state art = {0};

	assert(art_action_at(&game, 120, 90) == ACTION_LANGUAGE);
	assert(art_present(&art, &game, 0, 0, write_pixels) == 0);
	memcpy(reference, panel, sizeof(reference));
	game.chinese = true;
	assert(art_action_at(&game, 120, 90) == ACTION_LANGUAGE);
	assert(art_present(&art, &game, 0, 0, write_pixels) == 0);
	assert(art.chinese && memcmp(panel, reference, sizeof(panel)) != 0);
	game.chinese = false;
	assert(art_present(&art, &game, 0, 0, write_pixels) == 0);
	assert(!art.chinese && memcmp(panel, reference, sizeof(panel)) == 0);
}

int main(int argc, char **argv)
{
	struct game game = {0};
	bool routes_ok = true;

	for (unsigned int level = 0; level < MAZE_LEVELS; level++) {
		maze_load(&game, level);
		routes_ok &= test_route(game);
	}
	assert(routes_ok);
	test_physics();
	test_features();
	test_timed_gate();
	test_coins();
	test_adventure();
	test_challenges();
	test_renderer();
	test_mascot_renderer();
	test_dynamic_renderer();
	test_home_controls();
	test_arcade();
	test_arcade_renderer();
	test_motion_games();
	test_motion_renderer();
	test_language();
	puts("Physics, scoring, pause and dirty rectangles: OK");

	if (argc > 1) {
		game.chinese = argc > 2 && strcmp(argv[2], "zh") == 0;
		static const enum page cards[] = {
			PAGE_BRIEF, PAGE_WIN, PAGE_PAUSE, PAGE_QUIT, PAGE_SELECT,
			PAGE_EXTRAS, PAGE_CHALLENGES, PAGE_DATE, PAGE_SEED, PAGE_SKINS, PAGE_ACHIEVEMENTS,
			PAGE_WORLDS, PAGE_SETTINGS, PAGE_COUNTDOWN,
			PAGE_GAMES, PAGE_FAIL,
		};
		enum { FIRST_CARD = MAZE_LEVELS + 1, FIRST_ARCADE = FIRST_CARD + 5, END_ARCADE = FIRST_ARCADE + 5 };

		const unsigned int first_motion = FIRST_CARD + 5 + sizeof(cards) / sizeof(cards[0]);
		for (unsigned int index = 0; index < first_motion + 3; index++) {
			struct art_state art = {0};
			char name[256];

			maze_load(&game, index == 0 ? 0 : index <= MAZE_LEVELS ? index - 1 : 7);
			game.page = index == 0 ? PAGE_MENU : index <= MAZE_LEVELS ? PAGE_PLAY :
				index < FIRST_ARCADE ? cards[index - FIRST_CARD] :
				index < END_ARCADE ? PAGE_ARCADE : index < first_motion ? cards[index - FIRST_CARD - 5] : PAGE_PLAY;
			game.countdown = 3;
			game.growth.date = 20261119;
			game.edit_date = game.page == PAGE_DATE ? game.growth.date : 23456;
			game.collection_cursor = game.page == PAGE_SKINS ? SKIN_NEON : ACH_SURVIVOR;
			game.growth.days[0] = (struct daily_record){game.growth.date, 32800};
			game.wallet = 32;
			game.elapsed = index == MAZE_LEVELS + 2 ? 32800 : 0;
			game.new_record = true;
			if (game.page == PAGE_WIN) {
				game.stars = 3;
				game.run.coins = game.run.spawned;
				game.run.score = 370;
				game.run.max_combo = 6;
			}
			if (index >= FIRST_ARCADE && index < END_ARCADE) {
				const unsigned int scene = index - FIRST_ARCADE;
				enum mode mode = scene == 0 || scene == 4 ? MODE_COINS : scene;

				arcade_start(&game, mode);
				game.selected_mode = mode;
				if (scene == 0) {
					game.page = PAGE_ARCADE;
				} else if (scene == 4) {
					game.page = PAGE_WIN;
					game.elapsed = ARCADE_COIN_MS;
					game.arcade.score = 260;
					game.arcade.collected = 14;
					game.arcade.peak = 6;
				} else {
					game.elapsed = mode == MODE_BALANCE ? 12200 : 14100;
					game.ball = (struct point){112, 141};
					arcade_tick(&game, (struct point){0});
				}
			}
			if (index >= first_motion) {
				arcade_start(&game, MODE_PINBALL + index - first_motion);
				if (game.mode == MODE_SPACE) {
					arcade_trigger(&game);
					for (unsigned int tick = 0; tick < 25; tick++) {
						arcade_tick(&game, (struct point){0});
					}
				}
			}
			assert(art_present(&art, &game, 0, 6, write_pixels) == 0);
			(void)snprintf(name, sizeof(name), "%s/maze-%u.ppm", argv[1], index);
			FILE *file = fopen(name, "wb");

			assert(file);
			fprintf(file, "P6\n%d %d\n255\n", MAZE_WIDTH, MAZE_HEIGHT);
			for (int row = 0; row < MAZE_HEIGHT; row++) {
				for (int col = 0; col < MAZE_WIDTH; col++) {
					uint16_t pixel = panel[row][col];

					pixel = (uint16_t)((pixel >> 8) | (pixel << 8));
					fputc(((pixel >> 11) & 31) * 255 / 31, file);
					fputc(((pixel >> 5) & 63) * 255 / 63, file);
					fputc((pixel & 31) * 255 / 31, file);
				}
			}
			fclose(file);
		}
	}
	return 0;
}
