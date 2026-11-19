#include "art.h"
#include "hardware.h"
#include "arcade_store.h"

#include <math.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#ifndef MAZE_IMU_X_AXIS
#define MAZE_IMU_X_AXIS 1
#endif
#ifndef MAZE_IMU_Y_AXIS
#define MAZE_IMU_Y_AXIS 0
#endif
#ifndef MAZE_IMU_X_SIGN
#define MAZE_IMU_X_SIGN (-1)
#endif
#ifndef MAZE_IMU_Y_SIGN
#define MAZE_IMU_Y_SIGN 1
#endif

BUILD_ASSERT(MAZE_IMU_X_AXIS >= 0 && MAZE_IMU_X_AXIS <= 1);
BUILD_ASSERT(MAZE_IMU_Y_AXIS >= 0 && MAZE_IMU_Y_AXIS <= 1);
BUILD_ASSERT(MAZE_IMU_X_AXIS != MAZE_IMU_Y_AXIS);
BUILD_ASSERT(MAZE_IMU_X_SIGN == 1 || MAZE_IMU_X_SIGN == -1);
BUILD_ASSERT(MAZE_IMU_Y_SIGN == 1 || MAZE_IMU_Y_SIGN == -1);

enum {
	FRAME_MS = 33,
	CAL_SAMPLES = 30,
	CAL_TIMEOUT = 7500,
	HOLD_MS = 2000,
};

struct calibration {
	struct point center, mean, filter;
	float minimum[2], maximum[2];
	unsigned int count;
	int64_t start;
	enum page return_to;
	bool valid;
};

struct gesture {
	struct touch first;
	enum action action;
	enum page page;
	int64_t start;
	unsigned int failures;
	bool active, cancelled;
};

struct view {
	struct game game;
	unsigned int progress, pulse;
};

K_MUTEX_DEFINE(view_lock);
K_SEM_DEFINE(view_ready, 0, 1);
K_SEM_DEFINE(challenge_request, 0, 1);
K_SEM_DEFINE(challenge_ready, 0, 1);

static struct game game;
static struct game generated;
static struct view view;
static struct calibration calibration;
static struct gesture gesture;
static struct art_state art;
static unsigned int imu_errors;
static int64_t countdown_until;

static void render_thread(void *unused_a, void *unused_b, void *unused_c)
{
	struct view snapshot;

	ARG_UNUSED(unused_a);
	ARG_UNUSED(unused_b);
	ARG_UNUSED(unused_c);

	k_sem_take(&view_ready, K_FOREVER);
	int64_t next_frame = k_uptime_get();

	for (;;) {
		k_mutex_lock(&view_lock, K_FOREVER);
		snapshot = view;
		k_mutex_unlock(&view_lock);

		int ret = art_present(&art, &snapshot.game, snapshot.progress, snapshot.pulse,
				      panel_write);

		if (ret < 0) {
			printk("LCD write failed: %d\n", ret);
			k_msleep(500);
		}
		next_frame += FRAME_MS;
		int64_t now = k_uptime_get();

		if (next_frame <= now) {
			next_frame = now + 1;
		}
		k_msleep(next_frame - now);
	}
}

K_THREAD_DEFINE(maze_renderer, 6144, render_thread, NULL, NULL, NULL, 5, 0, 0);

static void challenge_thread(void *unused_a, void *unused_b, void *unused_c)
{
	ARG_UNUSED(unused_a);
	ARG_UNUSED(unused_b);
	ARG_UNUSED(unused_c);

	for (;;) {
		k_sem_take(&challenge_request, K_FOREVER);
		challenge_load(&generated, generated.expedition, generated.challenge_seed,
			       generated.challenge_tier);
		generated.page = PAGE_BRIEF;
		k_sem_give(&challenge_ready);
	}
}

K_THREAD_DEFINE(maze_generator, 3072, challenge_thread, NULL, NULL, NULL, 7, 0, 0);

static float map_tilt(float value)
{
	const float dead_zone = 0.05234f;
	static const float ranges[] = {0.50f, 0.36f, 0.66f};
	float range = ranges[game.settings.sensitivity];
	float magnitude = fmaxf(0, fminf(1, (fabsf(value) - dead_zone) / (range - dead_zone)));

	return copysignf(magnitude, value);
}

static void begin_calibration(int64_t now, enum page return_to)
{
	calibration.count = 0;
	calibration.start = now;
	calibration.return_to = return_to;
	game.velocity = (struct point){0};
	game.page = PAGE_CALIBRATE;
}

static struct point axes(const float accel[3])
{
	int sign = game.settings.rotated ? -1 : 1;

	return (struct point){sign * MAZE_IMU_X_SIGN * accel[MAZE_IMU_X_AXIS],
			      sign * MAZE_IMU_Y_SIGN * accel[MAZE_IMU_Y_AXIS]};
}

static void start_countdown(int64_t now)
{
	countdown_until = now + 3000;
	game.countdown = 3;
	game.page = PAGE_COUNTDOWN;
}

static void prepare_play(int64_t now)
{
	calibration.filter = (struct point){0};
	if (calibration.valid) {
		start_countdown(now);
	} else {
		begin_calibration(now, PAGE_COUNTDOWN);
	}
}

static void measure_zero(const float accel[3], int64_t now)
{
	const float motion_limit = 0.055f;
	struct point plane = axes(accel);
	float norm = accel[0] * accel[0] + accel[1] * accel[1] + accel[2] * accel[2];
	const float samples[] = {plane.x, plane.y};

	if (norm < 0.85f * 0.85f || norm > 1.15f * 1.15f || fabsf(accel[2]) < 0.92f) {
		calibration.count = 0;
		return;
	}
	if (calibration.count == 0) {
		calibration.mean = (struct point){0};
	}
	for (unsigned int axis = 0; axis < 2; axis++) {
		if (calibration.count == 0) {
			calibration.minimum[axis] = samples[axis];
			calibration.maximum[axis] = samples[axis];
		} else {
			calibration.minimum[axis] = fminf(calibration.minimum[axis], samples[axis]);
			calibration.maximum[axis] = fmaxf(calibration.maximum[axis], samples[axis]);
			if (calibration.maximum[axis] - calibration.minimum[axis] > motion_limit) {
				calibration.count = 0;
				return;
			}
		}
	}
	calibration.mean.x += plane.x;
	calibration.mean.y += plane.y;
	if (++calibration.count >= CAL_SAMPLES) {
		calibration.center.x = calibration.mean.x / CAL_SAMPLES;
		calibration.center.y = calibration.mean.y / CAL_SAMPLES;
		calibration.filter = (struct point){0};
		calibration.valid = true;
		game.page = calibration.return_to;
		if (game.page == PAGE_COUNTDOWN) {
			start_countdown(now);
		}
		printk("IMU zero calibrated\n");
	}
}

static enum action action_at(const struct touch *point)
{
	return art_action_at(&game, point->x, point->y);
}

static void start_level(unsigned int level, int64_t now)
{
	game.seed ^= (uint32_t)now;
	maze_load(&game, level);
	prepare_play(now);
}

static void start_expedition(enum expedition type, uint32_t seed, unsigned int tier)
{
	generated = game;
	generated.expedition = type;
	generated.challenge_seed = seed;
	generated.challenge_tier = tier > CHALLENGE_MAX_TIER ? CHALLENGE_MAX_TIER : tier;
	calibration.filter = (struct point){0};
	game.page = PAGE_GENERATE;
	k_sem_give(&challenge_request);
}

static void start_random(int64_t now)
{
	uint32_t seed = game.seed_input ? game.seed_input + game.random_streak :
		game.seed ^ (uint32_t)now ^ 0x53a99b21u;

	start_expedition(EXP_RANDOM, seed, game.random_streak);
}

static void start_challenge(enum mode mode, int64_t now)
{
	game.seed ^= (uint32_t)now;
	arcade_start(&game, mode);
	prepare_play(now);
}

static void restart_run(int64_t now)
{
	if (game.mode == MODE_MAZE) {
		if (game.expedition == EXP_STORY) {
			start_level(game.index, now);
		} else {
			start_expedition(game.expedition, game.challenge_seed, game.challenge_tier);
		}
	} else {
		start_challenge(game.mode, now);
	}
}

static void click(enum action action, int64_t now)
{
	if (action == ACTION_HOME) {
		if (game.page == PAGE_PAUSE) {
			game.page = PAGE_QUIT;
		} else {
			maze_leave(&game);
		}
		return;
	}
	switch (game.page) {
	case PAGE_MENU:
		if (action == ACTION_PRIMARY) {
			game.page = PAGE_GAMES;
		} else if (action == ACTION_SKINS) {
			game.collection_cursor = game.growth.skin;
			game.skins_back = PAGE_MENU;
			game.page = PAGE_SKINS;
		} else if (action == ACTION_SETTINGS) {
			game.page = PAGE_SETTINGS;
		}
		break;
	case PAGE_GAMES:
		if (action == ACTION_PRIMARY) {
			game.page = PAGE_WORLDS;
		} else if (action >= ACTION_COIN_GAME && action <= ACTION_SNAKE_GAME) {
			game.selected_mode = MODE_COINS + action - ACTION_COIN_GAME;
			game.page = PAGE_ARCADE;
		} else if (action == ACTION_EXTRAS) {
			game.page = PAGE_EXTRAS;
		}
		break;
	case PAGE_WORLDS:
		if (action >= ACTION_WORLD_1 && action <= ACTION_WORLD_5) {
			start_level(adventure_level((action - ACTION_WORLD_1) * WORLD_SIZE), now);
		} else if (action == ACTION_LEVELS) {
			game.index = adventure_resume(&game);
			game.page = PAGE_SELECT;
		}
		break;
	case PAGE_SETTINGS:
		switch (action) {
		case ACTION_LANGUAGE: game.chinese = !game.chinese; break;
		case ACTION_BRIGHTNESS:
			game.settings.brightness = (game.settings.brightness + 1) % 3;
			panel_brightness(game.settings.brightness);
			break;
		case ACTION_SENSITIVITY:
			game.settings.sensitivity = (game.settings.sensitivity + 1) % 3;
			break;
		case ACTION_HAND: game.settings.left_hand = !game.settings.left_hand; break;
		case ACTION_ROTATE:
			game.settings.rotated = !game.settings.rotated;
			calibration.valid = false;
			break;
		case ACTION_QUIET: game.settings.quiet = !game.settings.quiet; break;
		default: return;
		}
		game.growth_revision++;
		break;
	case PAGE_EXTRAS:
		if (action == ACTION_PRIMARY) {
			game.page = PAGE_CHALLENGES;
		} else if (action == ACTION_SECONDARY) {
			game.collection_cursor = game.growth.skin;
			game.skins_back = PAGE_EXTRAS;
			game.page = PAGE_SKINS;
		} else if (action == ACTION_TERTIARY) {
			game.collection_cursor = 0;
			game.page = PAGE_ACHIEVEMENTS;
		}
		break;
	case PAGE_CHALLENGES:
		if (action == ACTION_PRIMARY) {
			game.random_streak = 0;
			start_random(now);
		} else if (action == ACTION_SECONDARY && adventure_date_valid(game.growth.date)) {
			start_expedition(EXP_DAILY, game.growth.date, 3 + game.growth.date % 4);
		} else if (action == ACTION_SECONDARY || action == ACTION_EDIT) {
			game.edit_date = adventure_date_valid(game.growth.date) ? game.growth.date : 20260101;
			game.edit_field = 0;
			game.page = PAGE_DATE;
		} else if (action == ACTION_FOURTH) {
			game.edit_date = game.seed_input;
			game.edit_field = 0;
			game.page = PAGE_SEED;
		}
		break;
	case PAGE_DATE:
	case PAGE_SEED:
		if (action == ACTION_PRIMARY) {
			game.edit_field = (game.edit_field + 1) % (game.page == PAGE_DATE ? 3 : 5);
		} else if (action == ACTION_PREV || action == ACTION_NEXT) {
			int delta = action == ACTION_NEXT ? 1 : -1;

			if (game.page == PAGE_DATE) {
				game.edit_date = adventure_date_adjust(game.edit_date, game.edit_field, delta);
			} else {
				static const uint32_t places[] = {10000, 1000, 100, 10, 1};
				uint32_t place = places[game.edit_field];
				int old = game.edit_date / place % 10;
				int replacement = (old + delta + 10) % 10;

				game.edit_date = game.edit_date - old * place + replacement * place;
			}
		} else if (action == ACTION_SECONDARY) {
			if (game.page == PAGE_DATE) {
				game.growth.date = game.edit_date;
			} else {
				game.seed_input = game.edit_date;
			}
			game.growth_revision++;
			game.page = PAGE_CHALLENGES;
		} else if (action == ACTION_TERTIARY) {
			game.page = PAGE_CHALLENGES;
		}
		break;
	case PAGE_SKINS:
	case PAGE_ACHIEVEMENTS: {
		unsigned int count = game.page == PAGE_SKINS ? SKIN_COUNT : ACH_COUNT;

		if (action == ACTION_PREV) {
			game.collection_cursor = (game.collection_cursor + count - 1) % count;
		} else if (action == ACTION_NEXT) {
			game.collection_cursor = (game.collection_cursor + 1) % count;
		} else if (action == ACTION_PRIMARY && game.page == PAGE_SKINS) {
			adventure_buy(&game, game.collection_cursor);
			adventure_equip(&game, game.collection_cursor);
		} else if (action == ACTION_TERTIARY) {
			game.page = game.page == PAGE_SKINS ? game.skins_back : PAGE_EXTRAS;
		}
		break;
	}
	case PAGE_ARCADE:
		if (action == ACTION_PRIMARY) {
			start_challenge(game.selected_mode, now);
		} else if (action == ACTION_SNAKE_STYLE && game.selected_mode == MODE_SNAKE) {
			game.settings.snake_free = !game.settings.snake_free;
			game.growth_revision++;
		}
		break;
	case PAGE_SELECT:
		if (action == ACTION_PRIMARY) {
			start_level(game.index, now);
		} else if (action == ACTION_PREV) {
			game.index = adventure_level(adventure_position(game.index) + MAZE_LEVELS - 1);
		} else if (action == ACTION_NEXT) {
			game.index = adventure_level(adventure_position(game.index) + 1);
		} else if (action == ACTION_WORLDS) {
			game.page = PAGE_WORLDS;
		}
		break;
	case PAGE_BRIEF:
		if (action == ACTION_PRIMARY) {
			prepare_play(now);
		} else if (action == ACTION_SECONDARY) {
			game.page = game.expedition == EXP_STORY ? PAGE_SELECT : PAGE_CHALLENGES;
		}
		break;
	case PAGE_PLAY:
		if (action == ACTION_PAUSE) {
			game.page = PAGE_PAUSE;
		}
		break;
	case PAGE_PAUSE:
		if (action == ACTION_PRIMARY) {
			game.page = PAGE_PLAY;
		} else if (action == ACTION_REPLAY) {
			restart_run(now);
		}
		break;
	case PAGE_QUIT:
		if (action == ACTION_PRIMARY) {
			game.page = PAGE_PAUSE;
		}
		break;
	case PAGE_WIN:
		if (action == ACTION_WORLDS) {
			game.page = PAGE_WORLDS;
		} else if (action == ACTION_PRIMARY) {
			if (game.expedition == EXP_RANDOM) {
				game.random_streak++;
				start_random(now);
			} else if (game.expedition == EXP_STORY) {
				start_level(adventure_level(adventure_position(game.index) + 1), now);
			}
		} else if (action == ACTION_REPLAY) {
			restart_run(now);
		}
		break;
	case PAGE_FAIL:
		if (action == ACTION_PRIMARY) {
			restart_run(now);
		}
		break;
	case PAGE_IMU_ERROR:
		if (action == ACTION_PRIMARY) {
			begin_calibration(now, calibration.return_to);
		}
		break;
	default:
		break;
	}
}

static bool holding_to_calibrate(void)
{
	return gesture.active && !gesture.cancelled && gesture.page == game.page &&
	       gesture.action == ACTION_NONE && (game.page == PAGE_PAUSE || game.page == PAGE_PLAY);
}

static void poll_touch(int64_t now)
{
	struct touch point;
	int status = touch_read(&point);

	if (status < 0) {
		if (++gesture.failures >= 3) {
			gesture.cancelled = true;
		}
		return;
	}
	gesture.failures = 0;

	if (status == 0) {
		if (gesture.active && !gesture.cancelled && gesture.page == game.page &&
		    gesture.action != ACTION_NONE && gesture.action != ACTION_TRIGGER &&
		    now - gesture.start >= MAZE_TICK_MS) {
			click(gesture.action, now);
		}
		gesture.active = false;
		return;
	}
	if (game.settings.rotated) {
		point.x = MAZE_WIDTH - 1 - point.x;
		point.y = MAZE_HEIGHT - 1 - point.y;
	}

	if (!gesture.active) {
		gesture = (struct gesture){.first = point, .action = action_at(&point),
					   .page = game.page, .start = now, .active = true};
		if (gesture.action == ACTION_TRIGGER) {
			arcade_trigger(&game);
		}
	}
	int dx = point.x - gesture.first.x;
	int dy = point.y - gesture.first.y;

	if (dx * dx + dy * dy > 14 * 14 ||
	    (gesture.action != ACTION_NONE && action_at(&point) != gesture.action)) {
		gesture.cancelled = true;
	}
	if (holding_to_calibrate() && now - gesture.start >= HOLD_MS) {
		gesture.cancelled = true;
		begin_calibration(now, game.page);
	}
	if (game.mode == MODE_SPACE && gesture.page == game.page &&
	    gesture.action == ACTION_TRIGGER && !gesture.cancelled) {
		arcade_trigger(&game);
	}
}

static void tick(int64_t now)
{
	if (game.page == PAGE_COUNTDOWN) {
		if (now >= countdown_until) {
			game.page = PAGE_PLAY;
			game.countdown = 0;
		} else {
			game.countdown = (countdown_until - now + 999) / 1000;
		}
		return;
	}
	if (game.page == PAGE_PLAY || game.page == PAGE_CALIBRATE) {
		float accel[3];
		int ret = imu_read(accel);

		if (ret < 0) {
			if (++imu_errors == 25 && game.page == PAGE_PLAY) {
				calibration.valid = false;
				calibration.return_to = PAGE_PLAY;
				game.page = PAGE_IMU_ERROR;
				printk("IMU read failed: %d\n", ret);
			}
			calibration.count = 0;
		} else {
			imu_errors = 0;
			if (game.page == PAGE_CALIBRATE) {
				measure_zero(accel, now);
			} else if (!holding_to_calibrate()) {
				struct point plane = axes(accel);

				calibration.filter.x += (plane.x - calibration.center.x -
							 calibration.filter.x) * 0.24f;
				calibration.filter.y += (plane.y - calibration.center.y -
							 calibration.filter.y) * 0.24f;
				struct point tilt = {map_tilt(calibration.filter.x),
						     map_tilt(calibration.filter.y)};

				if (game.mode == MODE_MAZE) {
					maze_tick(&game, tilt);
				} else {
					arcade_tick(&game, tilt);
				}
			}
		}
	}
	if (game.page == PAGE_CALIBRATE && now - calibration.start >= CAL_TIMEOUT) {
		calibration.valid = false;
		game.page = PAGE_IMU_ERROR;
		printk("IMU calibration timed out\n");
	}
}

int main(void)
{
	int ret = touch_prepare();

	if (ret < 0) {
		printk("Touch startup pulse failed: %d\n", ret);
		return ret;
	}

	ret = panel_init();
	if (ret == 0) {
		ret = art_present(&art, &game, 0, 0, panel_write);
	}
	if (ret == 0) {
		ret = panel_show();
	}
	if (ret < 0) {
		printk("Round Display initialization failed: %d\n", ret);
		return ret;
	}
	ret = touch_init();
	if (ret < 0) {
		printk("Touch initialization failed: %d\n", ret);
		return ret;
	}
	maze_save_load(&game);
	panel_brightness(game.settings.brightness);
	printk("XIAO Motion Arcade V2.0.3 ready: GC9A01 SPI3, CHSC6X HW-I2C1, onboard IMU I2C2\n");

	int64_t next_tick = k_uptime_get();
	bool render_started = false;
	uint32_t saved_revision = game.growth_revision;

	for (;;) {
		int64_t now = k_uptime_get();
		unsigned int progress = 0;

		poll_touch(now);
		if (game.page == PAGE_GENERATE && k_sem_take(&challenge_ready, K_NO_WAIT) == 0) {
			game = generated;
		}
		tick(now);
		if (saved_revision != game.growth_revision) {
			maze_save_queue(&game);
			saved_revision = game.growth_revision;
		}
		if (game.page == PAGE_CALIBRATE) {
			progress = calibration.count * 20 / CAL_SAMPLES;
		} else if (holding_to_calibrate()) {
			progress = (now - gesture.start) * 20 / HOLD_MS;
		}

		k_mutex_lock(&view_lock, K_FOREVER);
		view.game = game;
		view.progress = progress;
		view.pulse = (now / 80) % 9;
		k_mutex_unlock(&view_lock);
		if (!render_started) {
			k_sem_give(&view_ready);
			render_started = true;
		}

		next_tick += MAZE_TICK_MS;
		now = k_uptime_get();
		if (next_tick <= now) {
			next_tick = now + MAZE_TICK_MS;
		}
		k_msleep(next_tick - now);
	}
}
