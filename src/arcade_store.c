#include "arcade_store.h"
#include "maze.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/kvss/nvs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/printk.h>

#define LEGACY_ID 0x1201
#define V13_ID 0x1301
#define V2_ID 0x2001
#define SAVE_ID 0x2101
#define SAVE_VERSION 3

enum { LEGACY_MODES = MODE_BALANCE + 1 };

struct legacy_records {
	uint32_t version;
	uint32_t best[LEGACY_MODES];
};

struct profile {
	uint32_t version;
	uint32_t arcade_best[LEGACY_MODES];
	uint32_t times[MAZE_LEVELS], scores[MAZE_LEVELS];
	uint32_t wallet, seed_input;
	struct growth growth;
};

struct save_v13 {
	struct profile profile;
	uint32_t checksum;
};

struct motion_profile {
	struct profile profile;
	uint32_t motion_best[MODE_COUNT - LEGACY_MODES];
	uint32_t snake_free_best;
	uint8_t snake_best_length[2];
	struct game_settings settings;
};

struct save_v2 {
	struct motion_profile data;
	uint32_t checksum;
};

struct save {
	struct motion_profile data;
	uint32_t chinese;
	uint32_t checksum;
};

BUILD_ASSERT(sizeof(struct save_v13) == 404);
BUILD_ASSERT(sizeof(struct save_v2) == 428);
BUILD_ASSERT(sizeof(struct legacy_records) == 20);

K_MUTEX_DEFINE(records_lock);
K_SEM_DEFINE(records_ready, 0, 1);

static struct nvs_fs records_fs;
static struct save pending;
static bool storage_ready;

static void records_writer(void *unused_a, void *unused_b, void *unused_c)
{
	ARG_UNUSED(unused_a);
	ARG_UNUSED(unused_b);
	ARG_UNUSED(unused_c);

	for (;;) {
		k_sem_take(&records_ready, K_FOREVER);
		k_mutex_lock(&records_lock, K_FOREVER);
		struct save snapshot = pending;

		k_mutex_unlock(&records_lock);
		int ret = nvs_write(&records_fs, SAVE_ID, &snapshot, sizeof(snapshot));

		if (ret < 0) {
			printk("Maze save failed: %d\n", ret);
		}
	}
}

K_THREAD_DEFINE(arcade_records_writer, 2048, records_writer, NULL, NULL, NULL, 6, 0, 0);

static uint32_t checksum(const void *save, size_t size)
{
	const uint8_t *bytes = (const uint8_t *)save;
	uint32_t hash = 2166136261u;

	for (size_t index = 0; index < size; index++) {
		hash = (hash ^ bytes[index]) * 16777619u;
	}
	return hash;
}

static void restore_profile(struct game *game, const struct profile *profile)
{
	memcpy(game->arcade_best, profile->arcade_best, sizeof(profile->arcade_best));
	memcpy(game->record, profile->times, sizeof(profile->times));
	memcpy(game->high_score, profile->scores, sizeof(profile->scores));
	game->wallet = profile->wallet;
	game->seed_input = profile->seed_input;
	game->growth = profile->growth;
}

static void restore_motion_profile(struct game *game, const struct motion_profile *profile)
{
	restore_profile(game, &profile->profile);
	memcpy(&game->arcade_best[LEGACY_MODES], profile->motion_best, sizeof(profile->motion_best));
	memcpy(game->snake_best_length, profile->snake_best_length, sizeof(profile->snake_best_length));
	game->snake_free_best = profile->snake_free_best;
	game->settings = profile->settings;
	if (game->settings.brightness >= 3 || game->settings.sensitivity >= 3) {
		game->settings = (struct game_settings){0};
	}
}

void maze_save_load(struct game *game)
{
	struct flash_pages_info page;
	const struct device *flash = PARTITION_DEVICE(storage_partition);
	const off_t offset = PARTITION_OFFSET(storage_partition);
	const size_t size = PARTITION_SIZE(storage_partition);
	struct save saved;

	if (!device_is_ready(flash) || flash_get_page_info_by_offs(flash, offset, &page) < 0 ||
	    offset != page.start_offset || size / page.size < 2 || size % page.size != 0) {
		printk("Maze save: RAM only (storage geometry)\n");
		return;
	}
	records_fs.flash_device = flash;
	records_fs.offset = offset;
	records_fs.sector_size = page.size;
	records_fs.sector_count = size / page.size;
	int ret = nvs_mount(&records_fs);

	if (ret < 0) {
		printk("Maze save: RAM only (mount %d)\n", ret);
		return;
	}
	storage_ready = true;
	ret = nvs_read(&records_fs, SAVE_ID, &saved, sizeof(saved));
	if (ret == sizeof(saved) && saved.data.profile.version == SAVE_VERSION &&
	    saved.checksum == checksum(&saved, offsetof(struct save, checksum))) {
		restore_motion_profile(game, &saved.data);
		game->chinese = saved.chinese == 1;
		return;
	}
	if (ret != -ENOENT) {
		printk("Maze save: ignored invalid or unknown data\n");
	}
	struct save_v2 previous_v2;

	ret = nvs_read(&records_fs, V2_ID, &previous_v2, sizeof(previous_v2));
	if (ret == sizeof(previous_v2) && previous_v2.data.profile.version == 2 &&
	    previous_v2.checksum == checksum(&previous_v2, offsetof(struct save_v2, checksum))) {
		restore_motion_profile(game, &previous_v2.data);
		maze_save_queue(game);
		printk("Maze save: imported V2 progress and settings\n");
		return;
	}
	struct save_v13 previous;

	ret = nvs_read(&records_fs, V13_ID, &previous, sizeof(previous));
	if (ret == sizeof(previous) && previous.profile.version == 1 &&
	    previous.checksum == checksum(&previous, offsetof(struct save_v13, checksum))) {
		restore_profile(game, &previous.profile);
		maze_save_queue(game);
		printk("Maze save: imported V1.3 progress and records\n");
		return;
	}
	struct legacy_records legacy;

	ret = nvs_read(&records_fs, LEGACY_ID, &legacy, sizeof(legacy));
	if (ret == sizeof(legacy) && legacy.version == 1) {
		memcpy(game->arcade_best, legacy.best, sizeof(legacy.best));
		maze_save_queue(game);
		printk("Maze save: imported V1.2 arcade records\n");
	}
}

void maze_save_queue(const struct game *game)
{
	if (!storage_ready) {
		return;
	}
	k_mutex_lock(&records_lock, K_FOREVER);
	struct motion_profile *data = &pending.data;

	data->profile.version = SAVE_VERSION;
	memcpy(data->profile.arcade_best, game->arcade_best, sizeof(data->profile.arcade_best));
	memcpy(data->profile.times, game->record, sizeof(data->profile.times));
	memcpy(data->profile.scores, game->high_score, sizeof(data->profile.scores));
	data->profile.wallet = game->wallet;
	data->profile.seed_input = game->seed_input;
	data->profile.growth = game->growth;
	memcpy(data->motion_best, &game->arcade_best[LEGACY_MODES], sizeof(data->motion_best));
	memcpy(data->snake_best_length, game->snake_best_length, sizeof(data->snake_best_length));
	data->snake_free_best = game->snake_free_best;
	data->settings = game->settings;
	pending.chinese = game->chinese;
	pending.checksum = checksum(&pending, offsetof(struct save, checksum));
	k_mutex_unlock(&records_lock);
	k_sem_give(&records_ready);
}
