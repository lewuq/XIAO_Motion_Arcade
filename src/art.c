#include "art.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define RGB(red, green, blue) (((red) << 11) | ((green) << 5) | (blue))
#define HEX(color) RGB(((color) >> 19) & 31, ((color) >> 10) & 63, ((color) >> 3) & 31)
#define ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))

enum {
	INK = HEX(0x253858),
	SOFT = HEX(0x536686),
	SKY = HEX(0xfff9ea),
	COURT = HEX(0xeaf6ff),
	GRID = HEX(0x9cbbd4),
	BORDER = HEX(0xa9caff),
	BLUE = HEX(0x4285f4),
	CYAN = HEX(0x55c8f2),
	TEAL = HEX(0x23649e),
	LIME = HEX(0x4ccb77),
	GOLD = HEX(0xffd447),
	COIN = HEX(0xffbf36),
	PURPLE = HEX(0xa477e8),
	RED = HEX(0xf25454),
	WHITE = HEX(0xffffff),
	BRONZE = HEX(0x895416),
	GREEN_INK = HEX(0x256345),
	RED_INK = HEX(0x9b3546),
};

struct rect {
	int x, y, w, h;
};

static uint16_t frame[MAZE_HEIGHT][MAZE_WIDTH];
static uint16_t transfer[240 * 16];
static struct rect scissor;
static bool chinese;

#define UI_FORMAT(buffer, english, han, ...) \
	((void)(chinese ? snprintf(buffer, sizeof(buffer), han, __VA_ARGS__) : \
			snprintf(buffer, sizeof(buffer), english, __VA_ARGS__)))

static int min(int first, int second)
{
	return first < second ? first : second;
}

static int max(int first, int second)
{
	return first > second ? first : second;
}

static struct rect clip(struct rect area, struct rect bounds)
{
	int left = max(area.x, bounds.x);
	int top = max(area.y, bounds.y);

	return (struct rect){left, top, min(area.x + area.w, bounds.x + bounds.w) - left,
			     min(area.y + area.h, bounds.y + bounds.h) - top};
}

static uint16_t lcd_color(uint16_t color)
{
	return (uint16_t)((color >> 8) | (color << 8));
}

static void fill(int x, int y, int width, int height, uint16_t color)
{
	struct rect area = clip((struct rect){x, y, width, height}, scissor);
	uint16_t pixel = lcd_color(color);

	for (int row = area.y; row < area.y + area.h; row++) {
		for (int col = area.x; col < area.x + area.w; col++) {
			frame[row][col] = pixel;
		}
	}
}

static void circle(int x, int y, int radius, uint16_t color)
{
	struct rect area = clip((struct rect){x - radius, y - radius, radius * 2 + 1,
					     radius * 2 + 1}, scissor);

	if (area.w <= 0) {
		return;
	}
	for (int row = area.y; row < area.y + area.h; row++) {
		int delta = row - y;
		int width = (int)sqrtf((float)(radius * radius - delta * delta));

		fill(x - width, row, width * 2 + 1, 1, color);
	}
}

static void ring(int x, int y, int radius, uint16_t color, uint16_t inside)
{
	circle(x, y, radius, color);
	circle(x, y, radius - 1, inside);
}

static void oval(int x, int y, int width, int height, uint16_t color)
{
	int start = max(scissor.y, y - height);
	int end = min(scissor.y + scissor.h - 1, y + height);

	for (int row = start; row <= end; row++) {
		float dy = (float)(row - y) / height;
		int span = (int)(width * sqrtf(1 - dy * dy));

		fill(x - span, row, 2 * span + 1, 1, color);
	}
}

static void triangle(struct point a, struct point b, struct point c, uint16_t color)
{
	const struct point vertices[] = {a, b, c, a};
	int top = max(scissor.y, min(a.y, min(b.y, c.y)));
	int bottom = min(scissor.y + scissor.h - 1, max(a.y, max(b.y, c.y)));

	for (int y = top; y <= bottom; y++) {
		int left = MAZE_WIDTH, right = -1;

		for (unsigned int edge = 0; edge < 3; edge++) {
			struct point p = vertices[edge], q = vertices[edge + 1];

			if (p.y != q.y && y >= min(p.y, q.y) && y <= max(p.y, q.y)) {
				int x = (int)(p.x + (q.x - p.x) * (y - p.y) / (q.y - p.y));

				left = min(left, x);
				right = max(right, x);
			}
		}
		if (left <= right) {
			fill(left, y, right - left + 1, 1, color);
		}
	}
}

static uint16_t mix(uint16_t from, uint16_t to, unsigned int quarters)
{
	unsigned int left = 4 - quarters;

	return RGB(((from >> 11) * left + (to >> 11) * quarters) / 4,
		   (((from >> 5) & 63) * left + ((to >> 5) & 63) * quarters) / 4,
		   ((from & 31) * left + (to & 31) * quarters) / 4);
}

static void line(struct point start, struct point end, int radius, uint16_t color)
{
	int left = (int)floorf(fminf(start.x, end.x)) - radius;
	int top = (int)floorf(fminf(start.y, end.y)) - radius;
	struct rect area = clip((struct rect){left, top,
		(int)ceilf(fmaxf(start.x, end.x)) + radius + 1 - left,
		(int)ceilf(fmaxf(start.y, end.y)) + radius + 1 - top}, scissor);
	float dx = end.x - start.x;
	float dy = end.y - start.y;
	float inverse = 1.0f / (dx * dx + dy * dy);
	uint16_t pixel = lcd_color(color);

	for (int row = area.y; row < area.y + area.h; row++) {
		for (int col = area.x; col < area.x + area.w; col++) {
			int center_x = col - MAZE_CENTER_X;
			int center_y = row - MAZE_CENTER_Y;
			float ox = col - start.x;
			float oy = row - start.y;
			float amount = fmaxf(0, fminf(1, (ox * dx + oy * dy) * inverse));
			float rx = ox - dx * amount;
			float ry = oy - dy * amount;

			if (rx * rx + ry * ry <= radius * radius &&
			    center_x * center_x + center_y * center_y <= MAZE_RADIUS * MAZE_RADIUS) {
				frame[row][col] = pixel;
			}
		}
	}
}

static const uint8_t alphabet[][7] = {
	{14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, {14,17,16,16,16,17,14},
	{30,17,17,17,17,17,30}, {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
	{14,17,16,23,17,17,15}, {17,17,17,31,17,17,17}, {14,4,4,4,4,4,14},
	{7,2,2,2,18,18,12}, {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
	{17,27,21,21,17,17,17}, {17,25,21,19,17,17,17}, {14,17,17,17,17,17,14},
	{30,17,17,30,16,16,16}, {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
	{15,16,16,14,1,1,30}, {31,4,4,4,4,4,4}, {17,17,17,17,17,17,14},
	{17,17,17,17,17,10,4}, {17,17,17,21,21,27,17}, {17,17,10,4,10,17,17},
	{17,17,10,4,4,4,4}, {31,1,2,4,8,16,31},
};

static const uint8_t digits[][7] = {
	{14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, {14,17,1,2,4,8,31},
	{30,1,1,14,1,1,30}, {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
	{14,16,16,30,17,17,14}, {31,1,2,4,8,8,8}, {14,17,17,14,17,17,14},
	{14,17,17,15,1,1,14},
};

static const uint8_t *glyph(char letter)
{
	static const uint8_t colon[] = {0,0,4,0,4,0,0};
	static const uint8_t period[] = {0,0,0,0,0,0,4};
	static const uint8_t dash[] = {0,0,0,14,0,0,0};
	static const uint8_t slash[] = {1,1,2,4,8,16,16};
	static const uint8_t plus[] = {0,4,4,31,4,4,0};
	static const uint8_t blank[7];

	if (letter >= 'A' && letter <= 'Z') {
		return alphabet[letter - 'A'];
	}
	if (letter >= '0' && letter <= '9') {
		return digits[letter - '0'];
	}
	switch (letter) {
	case ':': return colon;
	case '.': return period;
	case '-': return dash;
	case '/': return slash;
	case '+': return plus;
	default: return blank;
	}
}

static const char *translate(const char *text)
{
	static const struct { const char *english, *han; } labels[] = {
#define UI_TEXT(english, han) {english, han},
#include "ui_labels.inc"
#undef UI_TEXT
	};
	if (!chinese || (unsigned char)*text >= 0x80 || !*text) {
		return text;
	}
	size_t first = 0, last = ARRAY_COUNT(labels);
	while (first < last) {
		size_t middle = (first + last) / 2;
		int order = strcmp(text, labels[middle].english);

		if (order == 0) {
			return labels[middle].han;
		}
		if (order < 0) {
			last = middle;
		} else {
			first = middle + 1;
		}
	}
	return text;
}

enum { HAN_SIZE = 14, TEXT_PAD = (HAN_SIZE - 7) / 2 };

static const uint16_t *han_bitmap(unsigned int code)
{
	static const struct { uint16_t code, rows[HAN_SIZE]; } glyphs[] = {
#include "ui_glyphs.inc"
	};
	size_t first = 0, last = ARRAY_COUNT(glyphs);
	while (first < last) {
		size_t middle = (first + last) / 2;

		if (code == glyphs[middle].code) {
			return glyphs[middle].rows;
		}
		if (code < glyphs[middle].code) {
			last = middle;
		} else {
			first = middle + 1;
		}
	}
	return NULL;
}

static unsigned int next_code(const char **text)
{
	const unsigned char *bytes = (const unsigned char *)*text;
	unsigned int code = *bytes++;

	if ((code & 0xf0) == 0xe0 && (bytes[0] & 0xc0) == 0x80 && (bytes[1] & 0xc0) == 0x80) {
		code = ((code & 15) << 12) | ((bytes[0] & 63) << 6) | (bytes[1] & 63);
		bytes += 2;
	}
	*text = (const char *)bytes;
	return code;
}

static int text_width(const char *words, int scale)
{
	int width = 0, spacing = 0;

	while (*words) {
		bool han = next_code(&words) >= 0x80;

		spacing = han ? (scale + 1) / 2 : scale;
		width += (han ? HAN_SIZE + 1 : 6) * spacing;
	}
	return width - spacing;
}

static void centered(int center, int top, const char *words, int scale, uint16_t color)
{
	words = translate(words);
	int left = center - text_width(words, scale) / 2;
	int han_scale = (scale + 1) / 2;
	int han_top = top + (7 * scale - HAN_SIZE * han_scale) / 2;

	while (*words) {
		unsigned int code = next_code(&words);
		if (code >= 0x80) {
			const uint16_t *map = han_bitmap(code);

			if (map) {
				for (int row = 0; row < HAN_SIZE; row++) {
					for (int col = 0; col < HAN_SIZE; col++) {
						if (map[row] & (1u << col)) {
							fill(left + col * han_scale, han_top + row * han_scale,
							     han_scale, han_scale, color);
						}
					}
				}
			}
			left += (HAN_SIZE + 1) * han_scale;
			continue;
		}
		const uint8_t *map = glyph(code);

		for (int row = 0; row < 7; row++) {
			for (int col = 0; col < 5; col++) {
				if ((map[row] & (16 >> col)) != 0) {
					fill(left + col * scale, top + row * scale, scale, scale, color);
				}
			}
		}
		left += 6 * scale;
	}
}

static void outlined(int center, int top, const char *text, int scale, uint16_t color)
{
	for (int y = -2; y <= 2; y += 2) {
		for (int x = -2; x <= 2; x += 2) {
			centered(center + x, top + y, text, scale, INK);
		}
	}
	centered(center, top, text, scale, color);
}

static void capsule(int left, int top, int width, int height, uint16_t color)
{
	int radius = (height - 1) / 2;

	circle(left + radius, top + radius, radius, color);
	circle(left + width - radius - 1, top + radius, radius, color);
	fill(left + radius, top, width - 2 * radius, radius * 2 + 1, color);
}

static void button(struct rect area, uint16_t color, bool solid)
{
	capsule(area.x, area.y, area.w, area.h, INK);
	if (solid) {
		capsule(area.x + 2, area.y + 2, area.w - 4, area.h - 4, mix(color, INK, 1));
		capsule(area.x + 2, area.y + 2, area.w - 4, area.h - 7, color);
		capsule(area.x + area.h / 2, area.y + 4, min(31, area.w - area.h), 3,
			mix(color, WHITE, 2));
	} else {
		capsule(area.x + 2, area.y + 2, area.w - 4, area.h - 4, mix(SKY, color, 1));
	}
}

struct control {
	struct rect area;
	enum action action;
	const char *label;
	uint16_t color;
	uint8_t scale;
	bool solid;
};

#define PRIMARY(action_id, label, color) \
	{{ACTION_X, PRIMARY_Y, ACTION_WIDTH, ACTION_HEIGHT}, action_id, label, color, 2, true}
#define SECONDARY(action_id, label, color) \
	{{ACTION_X, SECONDARY_Y, ACTION_WIDTH, ACTION_HEIGHT}, action_id, label, color, 2, false}
#define SHORTCUT(left, action_id, label) \
	{{left, SECONDARY_Y, SPLIT_W, ACTION_HEIGHT}, action_id, label, BORDER, 1, true}
#define END_CONTROLS {{0}, ACTION_NONE, NULL, 0, 0, false}

static const struct control menu_controls[] = {
	{{47, 120, 146, 35}, ACTION_PRIMARY, "START", GOLD, 2, true},
	{{59, 160, 122, 29}, ACTION_SKINS, "SKINS", PURPLE, 2, true},
	{{53, 194, 134, 29}, ACTION_SETTINGS, "SETTINGS", CYAN, 2, true}, END_CONTROLS,
};
static const struct control game_controls[] = {
	{{48, 77, 144, 29}, ACTION_PRIMARY, "ADVENTURE", GOLD, 2, true},
	{{29, 111, 87, 27}, ACTION_COIN_GAME, "COIN RUSH", BORDER, 1, true},
	{{124, 111, 87, 27}, ACTION_DODGE_GAME, "DODGE", BORDER, 1, true},
	{{29, 143, 87, 27}, ACTION_BALANCE_GAME, "BALANCE", BORDER, 1, true},
	{{124, 143, 87, 27}, ACTION_PINBALL_GAME, "PINBALL", BORDER, 1, true},
	{{34, 175, 82, 27}, ACTION_SPACE_GAME, "SPACE RUN", BORDER, 1, true},
	{{124, 175, 82, 27}, ACTION_SNAKE_GAME, "SNAKE", BORDER, 1, true},
	{{82, 207, 76, 23}, ACTION_EXTRAS, "EXTRAS", PURPLE, 1, true}, END_CONTROLS,
};
static const struct control world_controls[] = {
	{{23, 77, 93, 33}, ACTION_WORLD_1, "", LIME, 1, true},
	{{124, 77, 93, 33}, ACTION_WORLD_2, "", CYAN, 1, true},
	{{23, 115, 93, 33}, ACTION_WORLD_3, "", RED, 1, true},
	{{124, 115, 93, 33}, ACTION_WORLD_4, "", GOLD, 1, true},
	{{66, 153, 108, 33}, ACTION_WORLD_5, "", PURPLE, 1, true},
	{{73, 192, 94, 29}, ACTION_LEVELS, "LEVELS", BORDER, 1, true}, END_CONTROLS,
};
static const struct control settings_controls[] = {
	{{54, 79, 132, 22}, ACTION_LANGUAGE, "LANGUAGE  ENGLISH", CYAN, 1, true},
	{{54, 103, 132, 22}, ACTION_BRIGHTNESS, "", BORDER, 1, true},
	{{54, 127, 132, 22}, ACTION_SENSITIVITY, "", BORDER, 1, true},
	{{54, 151, 132, 22}, ACTION_HAND, "", BORDER, 1, true},
	{{54, 175, 132, 22}, ACTION_ROTATE, "", BORDER, 1, true},
	{{54, 199, 132, 22}, ACTION_QUIET, "", BORDER, 1, true}, END_CONTROLS,
};
static const struct control extras_controls[] = {
	{{48, PRIMARY_Y, 144, ACTION_HEIGHT}, ACTION_PRIMARY, "CHALLENGES", CYAN, 2, true},
	SHORTCUT(SPLIT_X, ACTION_SECONDARY, "SKINS"),
	SHORTCUT(SPLIT_RIGHT, ACTION_TERTIARY, "BADGES"), END_CONTROLS,
};
static const struct control challenge_controls[] = {
	{{63, 130, 114, 27}, ACTION_PRIMARY, "RANDOM", CYAN, 2, true},
	{{63, 162, 114, 27}, ACTION_SECONDARY, "DAILY", BORDER, 2, false},
	{{61, 194, 55, 23}, ACTION_EDIT, "DATE", BORDER, 1, false},
	{{124, 194, 55, 23}, ACTION_FOURTH, "SEED", BORDER, 1, false}, END_CONTROLS,
};
static const struct control editor_controls[] = {
	{{63, 130, 114, 28}, ACTION_PRIMARY, "FIELD", CYAN, 2, true},
	{{57, 164, 59, 28}, ACTION_PREV, "MINUS", BORDER, 1, true},
	{{124, 164, 59, 28}, ACTION_NEXT, "PLUS", BORDER, 1, true},
	{{57, 198, 59, 26}, ACTION_TERTIARY, "BACK", BORDER, 1, false},
	{{124, 198, 59, 26}, ACTION_SECONDARY, "SAVE", LIME, 1, true}, END_CONTROLS,
};
static const struct control skin_controls[] = {
	PRIMARY(ACTION_PRIMARY, "SELECT", CYAN), SHORTCUT(SPLIT_X, ACTION_PREV, "PREV"),
	SHORTCUT(SPLIT_RIGHT, ACTION_NEXT, "NEXT"), END_CONTROLS,
};
static const struct control badge_controls[] = {
	SHORTCUT(SPLIT_X, ACTION_PREV, "PREV"), SHORTCUT(SPLIT_RIGHT, ACTION_NEXT, "NEXT"),
	{{82, FOOTER_Y, 76, FOOTER_HEIGHT}, ACTION_TERTIARY, "BACK", BORDER, 1, false}, END_CONTROLS,
};
static const struct control arcade_controls[] = {
	PRIMARY(ACTION_PRIMARY, "PLAY", CYAN), END_CONTROLS,
};
static const struct control snake_controls[] = {
	PRIMARY(ACTION_PRIMARY, "PLAY", CYAN),
	SECONDARY(ACTION_SNAKE_STYLE, "", BORDER), END_CONTROLS,
};
static const struct control select_controls[] = {
	{{72, 93, 96, 26}, ACTION_WORLDS, "WORLDS", CYAN, 1, true},
	{{40, 173, 49, 33}, ACTION_PREV, "PREV", BORDER, 1, true},
	{{151, 173, 49, 33}, ACTION_NEXT, "NEXT", BORDER, 1, true},
	{{93, 173, 54, 33}, ACTION_PRIMARY, "PLAY", LIME, 1, true}, END_CONTROLS,
};
static const struct control brief_controls[] = {
	PRIMARY(ACTION_PRIMARY, "ROLL", CYAN), SECONDARY(ACTION_SECONDARY, "LEVELS", BORDER), END_CONTROLS,
};
static const struct control challenge_brief_controls[] = {
	PRIMARY(ACTION_PRIMARY, "ROLL", CYAN), SECONDARY(ACTION_SECONDARY, "CHALLENGES", BORDER), END_CONTROLS,
};
static const struct control pause_controls[] = {
	PRIMARY(ACTION_PRIMARY, "CONTINUE", CYAN), SECONDARY(ACTION_REPLAY, "RESTART", BORDER), END_CONTROLS,
};
static const struct control quit_controls[] = {
	{{SPLIT_X, SECONDARY_Y, SPLIT_W, ACTION_HEIGHT}, ACTION_HOME, "QUIT", RED, 1, false},
	{{SPLIT_RIGHT, SECONDARY_Y, SPLIT_W, ACTION_HEIGHT}, ACTION_PRIMARY, "CANCEL", CYAN, 1, true},
	END_CONTROLS,
};
static const struct control win_controls[] = {
	PRIMARY(ACTION_PRIMARY, "NEXT", LIME), SECONDARY(ACTION_REPLAY, "REPLAY", BORDER), END_CONTROLS,
};
static const struct control world_win_controls[] = {
	PRIMARY(ACTION_WORLDS, "WORLDS", LIME), SECONDARY(ACTION_REPLAY, "REPLAY", BORDER), END_CONTROLS,
};
static const struct control fail_controls[] = {
	PRIMARY(ACTION_PRIMARY, "RETRY", GOLD), END_CONTROLS,
};
static const struct control arcade_end_controls[] = {
	PRIMARY(ACTION_PRIMARY, "RETRY", CYAN), END_CONTROLS,
};
static const struct control arcade_win_controls[] = {
	PRIMARY(ACTION_REPLAY, "AGAIN", LIME), END_CONTROLS,
};
static const struct control play_controls[] = {
	{{PAUSE_X, PAUSE_Y, PAUSE_WIDTH, PAUSE_HEIGHT}, ACTION_PAUSE, NULL, 0, 0, false}, END_CONTROLS,
};
static const struct control play_left_controls[] = {
	{{MAZE_WIDTH - PAUSE_X - PAUSE_WIDTH, PAUSE_Y, PAUSE_WIDTH, PAUSE_HEIGHT},
	 ACTION_PAUSE, NULL, 0, 0, false}, END_CONTROLS,
};

static const struct control *controls(const struct game *game)
{
	switch (game->page) {
	case PAGE_MENU: return menu_controls;
	case PAGE_GAMES: return game_controls;
	case PAGE_WORLDS: return world_controls;
	case PAGE_SETTINGS: return settings_controls;
	case PAGE_EXTRAS: return extras_controls;
	case PAGE_CHALLENGES: return challenge_controls;
	case PAGE_DATE:
	case PAGE_SEED: return editor_controls;
	case PAGE_SKINS: return skin_controls;
	case PAGE_ACHIEVEMENTS: return badge_controls;
	case PAGE_ARCADE: return game->selected_mode == MODE_SNAKE ? snake_controls : arcade_controls;
	case PAGE_SELECT: return select_controls;
	case PAGE_BRIEF: return game->expedition == EXP_STORY ? brief_controls : challenge_brief_controls;
	case PAGE_PLAY: return game->settings.left_hand ? play_left_controls : play_controls;
	case PAGE_PAUSE: return pause_controls;
	case PAGE_QUIT: return quit_controls;
	case PAGE_WIN:
		return game->mode != MODE_MAZE || game->expedition == EXP_DAILY ? arcade_win_controls :
			game->expedition == EXP_STORY && (adventure_position(game->index) + 1) % WORLD_SIZE == 0 ?
				world_win_controls : win_controls;
	case PAGE_FAIL: return game->mode != MODE_MAZE ? arcade_end_controls : fail_controls;
	case PAGE_IMU_ERROR: return arcade_end_controls;
	default: return NULL;
	}
}

enum { CORNER_X = 55, CORNER_Y = 54, CORNER_TOUCH = 44 };

static enum action corner_action(enum page page)
{
	switch (page) {
	case PAGE_SKINS: return ACTION_TERTIARY;
	case PAGE_GAMES: case PAGE_WORLDS: case PAGE_SETTINGS: case PAGE_EXTRAS:
	case PAGE_CHALLENGES: case PAGE_ARCADE: case PAGE_SELECT: case PAGE_COUNTDOWN:
	case PAGE_BRIEF: case PAGE_PAUSE: case PAGE_WIN: case PAGE_FAIL: case PAGE_IMU_ERROR:
		return ACTION_HOME;
	default: return ACTION_NONE;
	}
}

static bool contains(struct rect area, int x, int y)
{
	return x >= area.x && x < area.x + area.w && y >= area.y && y < area.y + area.h;
}

enum action art_action_at(const struct game *game, int x, int y)
{
	enum action corner = corner_action(game->page);
	struct rect target = {CORNER_X - CORNER_TOUCH / 2, CORNER_Y - CORNER_TOUCH / 2,
			      CORNER_TOUCH, CORNER_TOUCH};

	if (corner != ACTION_NONE && contains(target, x, y)) {
		return corner;
	}
	for (const struct control *entry = controls(game); entry && entry->action; entry++) {
		if (contains(entry->area, x, y)) {
			return entry->action;
		}
	}
	if (game->page == PAGE_PLAY && (game->mode == MODE_PINBALL || game->mode == MODE_SPACE) &&
	    x >= 22 && x < 218 && y >= 42 && y < 221) {
		return ACTION_TRIGGER;
	}
	return ACTION_NONE;
}

static void draw_corner(enum action action)
{
	if (action == ACTION_NONE) {
		return;
	}
	const int x = CORNER_X, y = CORNER_Y;
	uint16_t accent = action == ACTION_HOME ? GOLD : CYAN;

	circle(x, y, 17, INK);
	circle(x, y, 15, mix(accent, INK, 1));
	circle(x, y - 1, 14, accent);
	fill(x - 6, y - 12, 8, 1, mix(accent, WHITE, 2));
	if (action == ACTION_HOME) {
		triangle((struct point){x - 11, y}, (struct point){x, y - 9},
			 (struct point){x + 11, y}, INK);
		fill(x - 7, y - 1, 15, 10, INK);
		fill(x - 2, y + 3, 5, 6, accent);
	} else {
		triangle((struct point){x - 10, y}, (struct point){x, y - 8},
			 (struct point){x, y + 8}, INK);
		fill(x, y - 3, 10, 7, INK);
	}
}

static void menu_icon(enum action action, int x, int y)
{
	if (action == ACTION_PRIMARY) {
		triangle((struct point){x - 6, y - 7}, (struct point){x - 6, y + 7},
			 (struct point){x + 7, y}, INK);
		triangle((struct point){x - 4, y - 4}, (struct point){x - 4, y + 4},
			 (struct point){x + 3, y}, WHITE);
	} else if (action == ACTION_SKINS) {
		fill(x - 7, y - 5, 15, 7, INK);
		fill(x - 5, y - 7, 11, 14, INK);
		fill(x - 6, y - 4, 13, 4, WHITE);
		fill(x - 3, y - 6, 7, 11, WHITE);
		fill(x - 1, y - 6, 3, 2, PURPLE);
	} else {
		circle(x, y, 6, INK);
		fill(x - 2, y - 8, 5, 17, INK);
		fill(x - 8, y - 2, 17, 5, INK);
		ring(x, y, 4, WHITE, CYAN);
	}
}

static void draw_controls(const struct game *game)
{
	for (const struct control *entry = controls(game); entry && entry->action; entry++) {
		if (!entry->label) {
			continue;
		}
		struct rect area = entry->area;
		uint16_t text = !entry->solid && entry->color == RED ? RED_INK : INK;
		const char *label = entry->label;
		unsigned int skin = game->collection_cursor;
		char setting[24];
		static const char *const brightness[] = {"HIGH", "MID", "LOW"};
		static const char *const sensitivity[] = {"MID", "HIGH", "LOW"};

		if (entry->action >= ACTION_WORLD_1 && entry->action <= ACTION_WORLD_5) {
			label = adventure_world(adventure_level((entry->action - ACTION_WORLD_1) * WORLD_SIZE));
		}
		switch (entry->action) {
		case ACTION_BRIGHTNESS:
			UI_FORMAT(setting, "BRIGHTNESS  %s", "亮度  %s", translate(brightness[game->settings.brightness]));
			label = setting;
			break;
		case ACTION_SENSITIVITY:
			UI_FORMAT(setting, "TILT  %s", "灵敏度  %s", translate(sensitivity[game->settings.sensitivity]));
			label = setting;
			break;
		case ACTION_HAND: label = game->settings.left_hand ? "PAUSE  LEFT" : "PAUSE  RIGHT"; break;
		case ACTION_ROTATE: label = game->settings.rotated ? "ROTATION  180" : "ROTATION  0"; break;
		case ACTION_QUIET: label = game->settings.quiet ? "EFFECTS  LOW" : "EFFECTS  FULL"; break;
		case ACTION_SNAKE_STYLE: label = game->settings.snake_free ? "FREE TURN" : "CLASSIC"; break;
		default: break;
		}

		if (game->page == PAGE_SKINS && entry->action == ACTION_PRIMARY) {
			bool unlocked = skin == SKIN_GOLD || (game->growth.skins & MAZE_BIT(skin));

			label = unlocked ? "EQUIP" : adventure_skin_cost(skin) ? "BUY" : "LOCKED";
		}

		button(area, entry->color, entry->solid);
		label = translate(label);
		int label_center = area.x + area.w / 2;
		if (game->page == PAGE_MENU) {
			label_center += 8;
			int half_text = text_width(label, entry->scale) / 2;

			menu_icon(entry->action, label_center - half_text - 13, area.y + area.h / 2 - 1);
		}
		centered(label_center, area.y + (area.h - 7 * entry->scale) / 2 - entry->solid,
			 label, entry->scale, text);
	}
}

static void time_text(char result[20], uint32_t elapsed)
{
	unsigned int tenth = elapsed / 100;

	(void)snprintf(result, 20, "%02u:%02u.%u", min(tenth / 600, 99),
		       (tenth / 10) % 60, tenth % 10);
}

enum expression { FACE_SMILE, FACE_HAPPY, FACE_SURPRISED, FACE_SAD };

static void mascot(int x, int y, int radius, unsigned int skin, enum expression face)
{
	static const uint16_t colors[] = {GOLD, CYAN, HEX(0xff9362), HEX(0xec88d9), LIME, PURPLE};
	uint16_t color = colors[skin < SKIN_COUNT ? skin : SKIN_GOLD];
	int eye_x = max(2, radius / 3), eye = max(0, radius / 7 - 1);
	int cheek_x = radius * 3 / 5, cheek_y = y + max(1, radius / 5);
	int rim = radius > 10 ? 2 : 1;

	circle(x, y, radius, INK);
	circle(x, y, radius - rim, mix(color, BRONZE, 1));
	circle(x, y - 1, radius - rim - 1, color);
	circle(x - radius / 3, y - radius / 2, radius / 7, mix(color, WHITE, 3));
	if (radius > 10) {
		oval(x - cheek_x, cheek_y, radius / 6, radius / 10, RED);
		oval(x + cheek_x, cheek_y, radius / 6, radius / 10, RED);
	} else {
		fill(x - cheek_x, cheek_y, 1, 1, RED);
		fill(x + cheek_x, cheek_y, 1, 1, RED);
	}
	for (int side = -1; side <= 1; side += 2) {
		int ey = y - (face == FACE_SURPRISED ? max(1, radius / 5) : 1);
		int ex = x + side * eye_x;

		if (face == FACE_HAPPY && radius > 10) {
			fill(ex - 2, ey - 1, 5, 2, INK);
			fill(ex - 3, ey + 1, 2, 2, INK);
			fill(ex + 2, ey + 1, 2, 2, INK);
		} else {
			circle(ex, ey, eye, INK);
			if (eye >= 2) {
				fill(ex - 1, ey - 1, 1, 1, WHITE);
			}
		}
	}
	if (face == FACE_SURPRISED) {
		circle(x, y + max(2, radius / 4), max(1, radius / 6), INK);
	} else if (face == FACE_HAPPY && radius > 10) {
		oval(x, y + radius / 4, radius / 4, radius / 6, INK);
		oval(x, y + radius / 3, radius / 7, radius / 10, RED);
	} else if (face == FACE_SAD) {
		int width = max(2, radius / 5), mouth = y + max(2, radius / 4);

		fill(x - width, mouth, 2 * width + 1, 2, INK);
		fill(x - width - 1, mouth + 2, 2, 2, INK);
		fill(x + width, mouth + 2, 2, 2, INK);
	} else {
		fill(x - 1, y + 2, 3, 1, INK);
	}
}

static void ball(int x, int y, unsigned int skin)
{
	mascot(x, y, MAZE_BALL, skin, FACE_SMILE);
}

static void sparkle(int x, int y, int radius, uint16_t color)
{
	fill(x, y - radius, 1, radius * 2 + 1, color);
	fill(x - radius, y, radius * 2 + 1, 1, color);
	fill(x - 1, y - 1, 3, 3, color);
}

static void star(int x, int y, int scale, bool earned)
{
	static const uint16_t pixels[] = {
		0x020, 0x070, 0x070, 0x7ff, 0x3fe, 0x1fc, 0x0f8, 0x0f8, 0x1dc, 0x18c, 0x104,
	};
	for (unsigned int row = 0; row < ARRAY_COUNT(pixels); row++) {
		for (unsigned int col = 0; col < 11; col++) {
			uint16_t bit = 1u << col;

			if (!(pixels[row] & bit)) {
				continue;
			}
			bool interior = row && row + 1 < ARRAY_COUNT(pixels) &&
					(pixels[row - 1] & bit) && (pixels[row + 1] & bit) &&
					(pixels[row] & (bit >> 1)) && (pixels[row] & (bit << 1));

			fill(x + ((int)col - 5) * scale, y + ((int)row - 5) * scale, scale, scale,
			     interior ? (earned ? (row <= 5 ? GOLD : COIN) : BORDER) : INK);
		}
	}
}

static void round_edges(void)
{
	for (int y = scissor.y; y < scissor.y + scissor.h; y++) {
		int dy = y - 120;
		int s = 116 * 116 - dy * dy;
		int span = s > 0 ? sqrtf((float)s) : 0;

		if (span == 0) {
			fill(0, y, MAZE_WIDTH, 1, INK);
		} else {
			fill(0, y, 122 - span, 1, INK);
			fill(119 + span, y, 121 - span, 1, INK);
		}
	}
}

static void cloud(int x, int y)
{
	oval(x, y + 3, 24, 6, HEX(0xbcdef4));
	circle(x - 12, y, 8, WHITE);
	circle(x - 2, y - 5, 12, WHITE);
	circle(x + 12, y - 1, 9, WHITE);
	oval(x, y + 3, 23, 5, WHITE);
}

static void bush(int x, int y)
{
	oval(x, y + 3, 22, 12, GREEN_INK);
	circle(x - 9, y, 11, HEX(0x428f55));
	circle(x + 2, y - 7, 14, HEX(0x53b55c));
	circle(x + 12, y, 8, LIME);
	oval(x + 1, y - 12, 6, 2, HEX(0xa4de77));
}

static void flower(int x, int y)
{
	fill(x, y, 2, 7, GREEN_INK);
	circle(x - 2, y, 2, WHITE);
	circle(x + 3, y, 2, WHITE);
	circle(x, y - 2, 2, WHITE);
	circle(x, y, 2, COIN);
}

static void landscape(void)
{
	fill(0, 0, MAZE_WIDTH, MAZE_HEIGHT, CYAN);
	fill(0, 48, MAZE_WIDTH, 78, HEX(0x90def5));
	fill(0, 99, MAZE_WIDTH, 49, HEX(0xc4eff8));
	cloud(19, 64);
	cloud(184, 34);
	cloud(231, 72);
	triangle((struct point){127, 134}, (struct point){184, 77}, (struct point){231, 134}, HEX(0x7daac6));
	triangle((struct point){166, 95}, (struct point){184, 77}, (struct point){200, 96}, SKY);
	triangle((struct point){0, 139}, (struct point){53, 90}, (struct point){116, 139}, HEX(0x7daac6));
	oval(165, 214, 147, 89, HEX(0x90d767));
	oval(41, 210, 110, 82, HEX(0xaee178));
	oval(111, 244, 137, 59, HEX(0x76bc56));
	oval(124, 248, 49, 37, HEX(0xffdc91));
	bush(20, 123);
	bush(218, 143);
	bush(16, 179);
	bush(211, 185);
	flower(32, 147);
	flower(203, 152);
	flower(46, 194);
	flower(190, 204);
}

static void render_home(void)
{
	landscape();
	int title_top = chinese ? 29 : 33;
	int subtitle_top = chinese ? 61 : 59;
	centered(120, title_top + 3, "MOTION", 3, TEAL);
	centered(120, subtitle_top + 3, "ARCADE", 3, TEAL);
	outlined(120, title_top, "MOTION", 3, WHITE);
	outlined(120, subtitle_top, "ARCADE", 3, GOLD);
	star(49, 75, 1, true);
	star(190, 75, 1, true);
	sparkle(80, 93, 4, WHITE);
	sparkle(159, 98, 4, WHITE);
	oval(120, 115, 18, 3, GREEN_INK);
	triangle((struct point){126, 108}, (struct point){145, 111}, (struct point){134, 116}, INK);
	triangle((struct point){128, 109}, (struct point){142, 111}, (struct point){134, 114}, RED);
	mascot(120, 100, 16, SKIN_GOLD, FACE_HAPPY);
}

static void progress_bar(int y, unsigned int progress)
{
	capsule(72, y, 96, 7, BORDER);
	if (progress > 0) {
		capsule(72, y, 6 + progress * 90 / 20, 7, CYAN);
	}
}

static uint16_t key_color(unsigned int id)
{
	static const uint16_t colors[] = {COIN, PURPLE, LIME, CYAN};

	return colors[id % ARRAY_COUNT(colors)];
}

static void badge(struct point at, int radius, char symbol, uint16_t color)
{
	char label[] = {symbol, 0};

	ring((int)at.x, (int)at.y, radius, INK, color);
	centered((int)at.x, (int)at.y - 3, label, 1, INK);
}

static void arrow(struct point at, struct point direction, uint16_t color)
{
	struct point tip = {at.x + 5 * direction.x, at.y + 5 * direction.y};
	struct point tail = {at.x - 5 * direction.x, at.y - 5 * direction.y};
	struct point left = {at.x - 3 * direction.y, at.y + 3 * direction.x};
	struct point right = {at.x + 3 * direction.y, at.y - 3 * direction.x};

	line(tail, tip, 1, color);
	line(left, tip, 1, color);
	line(right, tip, 1, color);
}

static unsigned int lantern(const struct game *game)
{
	const struct level_spec *spec = game->level.spec;

	for (unsigned int idx = 0; idx < spec->feature_count; idx++) {
		struct feature configured = maze_feature(&game->level, idx);
		const struct feature *feature = &configured;

		if (feature->kind == MECH_DARK && maze_feature_on(game, feature)) {
			return feature->radius;
		}
	}
	return 0;
}

static uint16_t effects(const struct game *game)
{
	unsigned int slow = game->run.slow_until > game->elapsed ?
		(game->run.slow_until - game->elapsed + 999) / 1000 : 0;
	unsigned int magnet = game->run.magnet_until > game->elapsed ?
		(game->run.magnet_until - game->elapsed + 999) / 1000 : 0;

	return slow | (magnet << 4) | (game->run.shield ? 0x100 : 0) | (game->run.combo << 9);
}

static void portal(struct point at, int radius)
{
	ring(at.x, at.y, radius + 2, INK, PURPLE);
	ring(at.x, at.y, radius - 1, HEX(0xe7c7ff), HEX(0x533787));
	sparkle(at.x, at.y, 2, WHITE);
}

static void draw_features(const struct game *game, bool foreground)
{
	const struct level_spec *spec = game->level.spec;

	for (unsigned int idx = 0; idx < spec->feature_count; idx++) {
		struct feature configured = maze_feature(&game->level, idx);
		const struct feature *feature = &configured;
		struct point at = feature->a;

		if (!maze_feature_on(game, feature)) {
			continue;
		}
		if (!foreground) {
			switch (feature->kind) {
			case MECH_ICE: ring(at.x, at.y, feature->radius, CYAN, HEX(0xc9eeff)); break;
			case MECH_LOW_G: ring(at.x, at.y, feature->radius, HEX(0xb985d2), HEX(0xeadbf8)); break;
			case MECH_REVERSE: ring(at.x, at.y, feature->radius, RED, HEX(0xffdce5)); break;
			case MECH_BELT: ring(at.x, at.y, feature->radius, HEX(0x83bda4), HEX(0xd6f2df)); break;
			default: break;
			}
			continue;
		}
		switch (feature->kind) {
		case MECH_MOVING:
			line(feature->a, feature->b, 1, RED_INK);
			at = maze_mover(feature, maze_environment_time(game, game->elapsed));
			ring(at.x, at.y, feature->radius, INK, RED);
			circle(at.x, at.y, 2, SKY);
			break;
		case MECH_ONE_WAY: {
			struct point center = {(feature->a.x + feature->b.x) / 2,
					       (feature->a.y + feature->b.y) / 2};
			float dx = feature->b.x - feature->a.x, dy = feature->b.y - feature->a.y;
			float length = sqrtf(dx * dx + dy * dy);

			line(feature->a, feature->b, 1, TEAL);
			arrow(center, (struct point){-dy / length, dx / length}, LIME);
			break;
		}
		case MECH_PORTAL:
			portal(at, feature->radius);
			portal(feature->b, feature->radius);
			break;
		case MECH_LOW_G: badge(at, 7, 'G', PURPLE); break;
		case MECH_ICE: badge(at, 7, 'I', CYAN); break;
		case MECH_BELT: arrow(at, feature->b, CYAN); break;
		case MECH_REVERSE: badge(at, 7, 'R', RED); break;
		case MECH_ROTOR: {
			struct wall arm = maze_rotor(feature, maze_environment_time(game, game->elapsed));

			line(arm.start, arm.end, 3, INK);
			line(arm.start, arm.end, 2, RED);
			ring(at.x, at.y, 4, INK, COIN);
			break;
		}
		case MECH_KEY:
			if (feature->door) {
				if (!maze_has_key(game, feature->id)) {
					line(feature->a, feature->b, 3, INK);
					line(feature->a, feature->b, 2, key_color(feature->id));
				}
			} else if (!maze_has_key(game, feature->id)) {
				badge(at, 7, 'K', key_color(feature->id));
			}
			break;
		case MECH_TIMED: {
			struct point middle = {(at.x + feature->b.x) / 2, (at.y + feature->b.y) / 2};
			bool closed = maze_gate_active(game, feature);

			line(at, feature->b, closed ? 3 : 1, INK);
			if (closed) {
				line(at, feature->b, 2, RED);
			}
			ring(middle.x, middle.y, 3, INK, closed ? COIN : LIME);
			break;
		}
		default: break;
		}
	}
}

static void draw_items(const struct game *game)
{
	static const char symbols[] = {'S', 'T', 'M', 'B'};
	static const uint16_t colors[] = {CYAN, GOLD, LIME, PURPLE};
	const struct level_spec *spec = game->level.spec;

	for (unsigned int idx = 0; idx < spec->coin_count; idx++) {
		if (game->run.spawned & ~game->run.coins & MAZE_BIT(idx)) {
			struct point at = maze_transform(&game->level, spec->coins[idx]);

			ring(at.x, at.y, 4, BRONZE, COIN);
			fill(at.x - 1, at.y - 1, 1, 3, SKY);
		}
	}
	for (unsigned int idx = 0; idx < spec->item_count; idx++) {
		struct item configured = maze_item(&game->level, idx);
		const struct item *item = &configured;

		if (!(game->run.items & MAZE_BIT(idx))) {
			badge(item->at, 7, symbols[item->kind], colors[item->kind]);
		}
	}
}

static void fog(int x, int y, int radius)
{
	int radius_squared = radius * radius;
	const uint16_t shade = HEX(0x303055);

	for (int row = max(scissor.y, 27); row < min(scissor.y + scissor.h, 230); row++) {
		int dy = row - y;
		int half = radius_squared - dy * dy;
		int board_y = row - MAZE_CENTER_Y;
		int board_width = (int)sqrtf((float)(MAZE_RADIUS * MAZE_RADIUS - board_y * board_y));
		int start = MAZE_CENTER_X - board_width, end = MAZE_CENTER_X + board_width + 1;

		if (half <= 0) {
			fill(start, row, end - start, 1, shade);
		} else {
			int width = (int)sqrtf((float)half);
			int left = min(end, x - width), right = max(start, x + width + 1);

			fill(start, row, max(0, left - start), 1, shade);
			fill(right, row, max(0, end - right), 1, shade);
		}
	}
}

static int digit_center(const struct game *game)
{
	return game->expedition == EXP_STORY && adventure_position(game->index) >= 9 ? 86 : 82;
}

static void draw_pause(const struct game *game)
{
	int x = controls(game)->area.x;

	button((struct rect){x, PAUSE_Y, PAUSE_WIDTH, PAUSE_HEIGHT}, GOLD, true);
	fill(x + 9, PAUSE_Y + 6, 3, 9, INK);
	fill(x + 15, PAUSE_Y + 6, 3, 9, INK);
}

struct landscape_colors { uint16_t floor, marks, wall, edge; };

static const struct landscape_colors regions[] = {
	{HEX(0xfff9ea), HEX(0xd9edba), LIME, GREEN_INK},
	{HEX(0xe8f8ff), HEX(0xbbe7f7), BLUE, INK},
	{HEX(0xffeedb), HEX(0xf6c5a5), HEX(0xf58153), HEX(0x713642)},
	{HEX(0xfff6cd), HEX(0xf6dc8c), BLUE, INK},
	{HEX(0x40316a), HEX(0x7764a6), PURPLE, HEX(0x231e46)},
};

static unsigned int world_index(const struct game *game)
{
	return adventure_position(game->index) / WORLD_SIZE;
}

static void terrain(unsigned int world, const struct landscape_colors *colors)
{
	ring(MAZE_CENTER_X, MAZE_CENTER_Y, MAZE_RADIUS, INK, colors->floor);
	for (int y = 43; y <= 207; y += 18) {
		for (int x = 33 + (y / 18 % 2) * 8; x <= 208; x += 22) {
			int dx = x - MAZE_CENTER_X, dy = y - MAZE_CENTER_Y;

			if (dx * dx + dy * dy > 92 * 92 || x + 3 < scissor.x || x - 3 >= scissor.x + scissor.w ||
			    y + 3 < scissor.y || y - 3 >= scissor.y + scissor.h) {
				continue;
			}
			switch (world) {
			case 0:
				fill(x - 2, y, 2, 1, colors->marks);
				fill(x, y + 1, 1, 2, colors->marks);
				fill(x + 1, y - 1, 2, 2, colors->marks);
				break;
			case 1: ring(x, y, 2, colors->marks, colors->floor); break;
			case 2: fill(x - 2, y, 4, 2, colors->marks); break;
			case 3:
				fill(x, y - 2, 2, 2, colors->marks);
				fill(x - 1, y, 3, 1, colors->marks);
				fill(x - 1, y + 1, 2, 1, colors->marks);
				break;
			default: sparkle(x, y, 2, colors->marks); break;
			}
		}
	}
}

static void goal_flag(struct point at)
{
	int x = at.x, y = at.y;

	ring(x, y + 4, 7, GREEN_INK, HEX(0xb6e9a5));
	fill(x - 4, y - 10, 2, 16, INK);
	triangle((struct point){x - 3, y - 11}, (struct point){x - 3, y + 1},
		 (struct point){x + 10, y - 5}, INK);
	triangle((struct point){x - 1, y - 8}, (struct point){x - 1, y - 2},
		 (struct point){x + 6, y - 5}, LIME);
}

static void render_maze(const struct game *game, unsigned int progress)
{
	const struct level *level = &game->level;
	char timer[20];
	char status[30];
	char level_id[6];

	unsigned int world = world_index(game);
	const struct landscape_colors *colors = &regions[world];

	terrain(world, colors);
	draw_features(game, false);
	for (unsigned int idx = 0; idx < level->wall_count; idx++) {
		const struct wall *wall = &level->walls[idx];

		line(wall->start, wall->end, MAZE_WALL + 1, colors->edge);
	}
	for (unsigned int idx = 0; idx < level->wall_count; idx++) {
		const struct wall *wall = &level->walls[idx];

		line(wall->start, wall->end, MAZE_WALL - 1, colors->wall);
	}
	ring((int)level->start.x, (int)level->start.y, 9, colors->edge, colors->floor);
	goal_flag(level->goal);
	for (unsigned int idx = 0; idx < level->trap_count; idx++) {
		const struct trap *trap = &level->traps[idx];

		ring((int)trap->center.x, (int)trap->center.y, trap->radius, INK, RED);
		centered((int)trap->center.x, (int)trap->center.y - 3, "X", 1, WHITE);
	}
	draw_features(game, true);
	draw_items(game);
	if (!game->settings.quiet && game->run.flash_until > game->elapsed) {
		int radius = 5 + (420 - (game->run.flash_until - game->elapsed)) / 60;
		int x = game->run.flash.x, y = game->run.flash.y;

		fill(x - radius, y, 3, 1, GOLD);
		fill(x + radius - 2, y, 3, 1, GOLD);
		fill(x, y - radius, 1, 3, INK);
		fill(x, y + radius - 2, 1, 3, INK);
	}
	int ball_x = (int)(game->ball.x + 0.5f), ball_y = (int)(game->ball.y + 0.5f);
	unsigned int light = lantern(game);

	if (light) {
		fog(ball_x, ball_y, light);
	}
	if (game->run.shield) {
		ring(ball_x, ball_y, 8, CYAN, COURT);
	}
	mascot(ball_x, ball_y, MAZE_BALL, game->growth.skin,
	       game->run.react_until > game->elapsed ? FACE_SURPRISED : FACE_SMILE);

	(void)snprintf(level_id, sizeof(level_id), "%s%u",
		       chinese ? (game->expedition == EXP_DAILY ? "日" : game->expedition == EXP_RANDOM ? "随" : "关") :
			(game->expedition == EXP_DAILY ? "D" : game->expedition == EXP_RANDOM ? "R" : "L"),
		       game->expedition == EXP_STORY ? adventure_position(game->index) + 1 :
		       (unsigned int)game->challenge_tier + 1);
	centered(digit_center(game), 19, level_id, 1, TEAL);
	time_text(timer, game->elapsed);
	centered(123, 16, timer, 1, INK);
	draw_pause(game);

	if (progress > 0) {
		capsule(65, 202, 110, 23, SKY);
		centered(120, 205, "HOLD FLAT", 1, INK);
		progress_bar(215, progress);
	} else if (game->run.spawned) {
		uint16_t active = effects(game);

		capsule(74, 209, 92, 12, SKY);
		UI_FORMAT(status, "C %u/%u  P %lu", "币%u/%u 分%lu", maze_coin_count(game->run.coins),
			       maze_coin_count(game->run.spawned), (unsigned long)game->run.score);
		centered(120, 211, status, 1, BRONZE);
		if (active || (game->run.flash_until > game->elapsed && game->run.gain)) {
			capsule(76, 222, 88, 10, SKY);
			if (game->run.flash_until > game->elapsed && game->run.gain) {
				UI_FORMAT(status, "+%u  COMBO %u", "+%u 连击%u",
					       game->run.gain, game->run.combo);
			} else {
				UI_FORMAT(status, "S%u T%u M%u X%u", "盾%u 慢%u 磁%u X%u",
					       (active >> 8) & 1, active & 15, (active >> 4) & 15,
					       game->run.combo);
			}
			centered(120, 224, status, 1, TEAL);
		}
	}
}

static void render_arena(const struct game *game, unsigned int progress)
{
	const struct arcade_run *run = &game->arcade;
	const struct point center = {MAZE_CENTER_X, MAZE_CENTER_Y};
	char status[44], timer[20];

	ring(center.x, center.y, MAZE_RADIUS, TEAL, COURT);
	if (game->mode == MODE_BALANCE) {
		if (run->safe_radius) {
			ring(center.x, center.y, run->safe_radius, GREEN_INK, HEX(0xcdeedb));
		}
		fill(center.x - 4, center.y, 9, 1, TEAL);
		fill(center.x, center.y - 4, 1, 9, TEAL);
		if (run->wind.x || run->wind.y) {
			struct point at = {center.x + run->wind.x * 74, center.y + run->wind.y * 74};

			arrow(at, run->wind, GOLD);
		}
	}
	if (game->mode == MODE_COINS) {
		for (unsigned int slot = 0; slot < ARCADE_COIN_SLOTS; slot++) {
			const struct arcade_coin *coin = &run->coins[slot];

			if (!coin->respawn) {
				ring(coin->at.x, coin->at.y, 4, coin->bonus ? LIME : GOLD, SKY);
				if (coin->bonus) {
					fill(coin->at.x - 1, coin->at.y, 3, 1, LIME);
				}
			}
		}
	}
	for (unsigned int slot = 0; slot < run->enemies_active; slot++) {
		const struct arcade_enemy *enemy = &run->enemies[slot];
		static const uint16_t colors[] = {RED, RGB(31, 31, 7), RGB(31, 26, 24)};

		ring(enemy->at.x, enemy->at.y, ARCADE_ENEMY_RADIUS, colors[enemy->kind], SKY);
		circle(enemy->at.x, enemy->at.y, 2, colors[enemy->kind]);
	}
	if (!game->settings.quiet && run->flash_until > game->elapsed) {
		int x = run->flash.x, y = run->flash.y;

		fill(x - 8, y, 4, 1, GOLD);
		fill(x + 5, y, 4, 1, GOLD);
		fill(x, y - 8, 1, 4, INK);
		fill(x, y + 5, 1, 4, INK);
	}
	int ball_x = (int)(game->ball.x + 0.5f), ball_y = (int)(game->ball.y + 0.5f);

	if (run->immune_until > game->elapsed) {
		ring(ball_x, ball_y, 8, CYAN, COURT);
	}
	ball(ball_x, ball_y, game->growth.skin);
	uint32_t clock = game->mode == MODE_COINS ?
		ARCADE_COIN_MS - (game->elapsed < ARCADE_COIN_MS ? game->elapsed : ARCADE_COIN_MS) :
		game->elapsed;

	time_text(timer, clock);
	centered(120, 15, timer, 1, INK);
	centered(112, 33, arcade_name(game->mode), 1, TEAL);
	draw_pause(game);
	capsule(65, 203, 110, 23, SKY);
	if (progress) {
		centered(120, 205, "HOLD FLAT", 1, INK);
		progress_bar(215, progress);
	} else {
		switch (game->mode) {
		case MODE_COINS:
			UI_FORMAT(status, "C%u  P%lu  X%u", "币%u 分%lu X%u", run->collected,
				       (unsigned long)run->score, run->combo);
			break;
		case MODE_SURVIVAL:
			UI_FORMAT(status, "LIVES %u  P%lu", "命%u 分%lu", run->lives,
				       (unsigned long)run->score);
			break;
		default:
			(void)snprintf(status, sizeof(status), "%s", run->wind.x || run->wind.y ?
				       "WIND INCOMING" : "STAY IN THE RING");
			break;
		}
		centered(120, 211, status, 1, game->mode == MODE_BALANCE ? GREEN_INK : BRONZE);
	}
}

static void render_motion(const struct game *game, unsigned int progress)
{
	const struct arcade_run *run = &game->arcade;
	const struct point at = {(int)game->ball.x, (int)game->ball.y};
	char status[44];

	ring(MAZE_CENTER_X, MAZE_CENTER_Y, MAZE_RADIUS, TEAL, COURT);
	switch (game->mode) {
	case MODE_PINBALL: {
		bool flip = game->elapsed < run->pinball.flip_until;

		for (unsigned int idx = 0; idx < PIN_BUMPERS; idx++) {
			struct point bumper = pinball_bumpers[idx];

			ring(bumper.x, bumper.y, 11, idx == 1 ? GOLD : CYAN, SKY);
			circle(bumper.x, bumper.y, 4, idx == 1 ? GOLD : CYAN);
		}
		line((struct point){87, 194}, (struct point){111, flip ? 188 : 204}, 2, flip ? LIME : CYAN);
		line((struct point){153, 194}, (struct point){129, flip ? 188 : 204}, 2, flip ? LIME : CYAN);
		line((struct point){105, 212}, (struct point){135, 212}, 1, RED);
		if (run->pinball.loaded) {
			centered(120, 137, "TAP TO LAUNCH", 1, INK);
		}
		ball(at.x, at.y, game->growth.skin);
		UI_FORMAT(status, "L%u  P%lu  X%u", "命%u 分%lu X%u", run->lives,
			       (unsigned long)run->score, run->combo ? run->combo : 1);
		break;
	}
	case MODE_SPACE: {
		const struct space_run *space = &run->space;
		struct point nose = {at.x, at.y - 7}, left = {at.x - 5, at.y + 5}, right = {at.x + 5, at.y + 5};

		for (unsigned int idx = 0; idx < SPACE_ROCKS; idx++) {
			const struct space_object *rock = &space->rocks[idx];

			if (rock->radius) {
				ring(rock->at.x, rock->at.y, rock->radius, RED, RGB(11, 10, 13));
				circle(rock->at.x - 2, rock->at.y - 2, 2, SOFT);
			}
		}
		for (unsigned int idx = 0; idx < SPACE_SHOTS; idx++) {
			const struct space_object *shot = &space->shots[idx];

			if (shot->radius) {
				fill(shot->at.x - 1, shot->at.y - 3, 3, 6, GOLD);
			}
		}
		if (space->pickup.radius) {
			badge(space->pickup.at, 7, space->pickup_shield ? 'S' : '+', space->pickup_shield ? CYAN : LIME);
		}
		if (space->shield || run->immune_until > game->elapsed) {
			ring(at.x, at.y, 10, space->shield ? LIME : CYAN, COURT);
		}
		line(nose, left, 1, BLUE);
		line(nose, right, 1, BLUE);
		line(left, right, 1, INK);
		UI_FORMAT(status, "L%u  P%lu  HIT %u", "命%u 分%lu 击中%u", run->lives,
			       (unsigned long)run->score, run->collected);
		break;
	}
	default: {
		const struct snake_run *snake = &run->snake;
		int left = SNAKE_LEFT - 5, top = SNAKE_TOP - 5;
		int width = SNAKE_RIGHT - SNAKE_LEFT + 11, height = SNAKE_BOTTOM - SNAKE_TOP + 11;

		fill(left, top, width, 1, BORDER);
		fill(left, top + height - 1, width, 1, BORDER);
		fill(left, top, 1, height, BORDER);
		fill(left + width - 1, top, 1, height, BORDER);
		ring(snake->food.x, snake->food.y, 5, BRONZE, COIN);
		for (int idx = snake->length - 1; idx >= 0; idx--) {
			struct point segment = snake->body[idx];

			ring(segment.x, segment.y, idx ? 4 : 5, GREEN_INK, idx ? LIME : CYAN);
		}
		UI_FORMAT(status, "LENGTH %u  P%lu", "长度%u 分%lu", snake->length, (unsigned long)run->score);
		break;
	}
	}
	if (!game->settings.quiet && run->flash_until > game->elapsed) {
		static const struct point directions[] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {0.7f, 0.7f}, {-0.7f, -0.7f}};
		float distance = 6 + (240 - (run->flash_until - game->elapsed)) / 25.0f;

		for (unsigned int idx = 0; idx < ARRAY_COUNT(directions); idx++) {
			circle(run->flash.x + directions[idx].x * distance,
			       run->flash.y + directions[idx].y * distance, 1, idx % 2 ? GOLD : INK);
		}
	}
	centered(120, 33, arcade_name(game->mode), 1, TEAL);
	draw_pause(game);
	if (progress) {
		capsule(65, 203, 110, 23, SKY);
		centered(120, 205, "HOLD FLAT", 1, INK);
		progress_bar(215, progress);
	} else {
		capsule(57, 216, 126, 17, SKY);
		centered(120, 221, status, 1, BRONZE);
	}
}

static void arcade_result(const struct game *game, unsigned int pulse)
{
	char stats[48], timer[20];
	const struct arcade_run *run = &game->arcade;
	bool points = game->mode == MODE_COINS || game->mode >= MODE_PINBALL;
	bool won = game->page == PAGE_WIN;
	const char *result = game->mode == MODE_COINS ? "TIME UP" : game->mode == MODE_BALANCE ? "OUT OF BOUNDS" :
		game->mode == MODE_SNAKE ? (won ? "MAX LENGTH" : "COLLISION") : "NO LIVES LEFT";

	uint16_t accent = won ? LIME : RED;

	ring(120, 40, 6 + pulse / 2, accent, SKY);
	circle(120, 40, 2, accent);
	centered(120, 57, arcade_name(game->mode), 1, TEAL);
	centered(120, 76, result, 2, won ? GREEN_INK : RED_INK);
	if (points) {
		UI_FORMAT(stats, "PTS %lu", "得分 %lu", (unsigned long)run->score);
		centered(120, 98, stats, 2, BRONZE);
		if (game->mode == MODE_SNAKE) {
			UI_FORMAT(stats, "LENGTH %u  BEST %u", "长度%u 最长%u", run->snake.length,
				       game->snake_best_length[run->snake.free_turn]);
		} else if (game->mode == MODE_SPACE) {
			UI_FORMAT(stats, "ROCKS HIT %u", "击碎%u颗陨石", run->collected);
		} else if (game->mode == MODE_PINBALL) {
			UI_FORMAT(stats, "MAX MULTIPLIER X%u", "最高倍率 X%u", run->peak ? run->peak : 1);
		} else {
			UI_FORMAT(stats, "COINS %u  MAX X%u", "金币%u 最高连击%u", run->collected, run->peak);
		}
	} else {
		time_text(timer, game->elapsed);
		centered(120, 98, timer, 2, GREEN_INK);
		UI_FORMAT(stats, "POINTS %lu", "得分 %lu", (unsigned long)run->score);
	}
	centered(120, 122, stats, 1, INK);
	if (game->new_record) {
		centered(120, 215, "NEW BEST", 1, BRONZE);
	} else if (points) {
		UI_FORMAT(stats, "BEST %lu", "最佳 %lu",
			       (unsigned long)arcade_best_score(game, game->mode));
		centered(120, 215, stats, 1, SOFT);
	} else {
		time_text(timer, game->arcade_best[game->mode] * 100);
		UI_FORMAT(stats, "BEST %s", "最佳 %s", timer);
		centered(120, 215, stats, 1, SOFT);
	}
}

static void maze_result(const struct game *game, unsigned int pulse)
{
	bool won = game->page == PAGE_WIN;
	unsigned int flicker = pulse / 3;
	uint16_t upper = won ? HEX(0x90ddf6) : HEX(0x795bb6);
	uint16_t lower = won ? HEX(0xc7effa) : HEX(0x4e387f);
	uint16_t ray = won ? HEX(0xdff7ff) : HEX(0x8b6dc2);
	char time[20], text[48];

	fill(0, 0, 240, 240, upper);
	fill(0, 112, 240, 128, lower);
	for (int x = -38; x <= 240; x += 47) {
		triangle((struct point){x, 0}, (struct point){x + 20, 0}, (struct point){120, 113}, ray);
	}
	triangle((struct point){0, 91}, (struct point){0, 150}, (struct point){120, 113}, ray);
	triangle((struct point){239, 94}, (struct point){239, 151}, (struct point){120, 113}, ray);
	if (!won) {
		mascot(120, 64, 25, game->growth.skin, FACE_SAD);
		oval(86 - flicker, 61 + flicker, 3, 5, CYAN);
		oval(154 + flicker, 61 + flicker, 3, 5, CYAN);
		sparkle(67, 88, 3, WHITE);
		sparkle(171, 42, 3, WHITE);
		outlined(120, 99, "TRY AGAIN", 2, WHITE);
		centered(120, 123, game->level.name, 1, WHITE);
		return;
	}
	star(89, 48, 2, game->stars >= 1);
	star(120, 36 - (int)flicker, 3, game->stars >= 2);
	star(153, 48, 2, game->stars >= 3);
	sparkle(62, 66, 3 + flicker, WHITE);
	sparkle(177, 64, 3 + flicker, WHITE);
	mascot(120, 76, 16, game->growth.skin, FACE_HAPPY);
	bool world_done = game->expedition == EXP_STORY && (adventure_position(game->index) + 1) % WORLD_SIZE == 0;
	outlined(120, 98, world_done ? "WORLD CLEAR" : "LEVEL CLEAR", 2, WHITE);
	capsule(45, 117, 150, 17, INK);
	capsule(47, 119, 146, 13, SKY);
	time_text(time, game->elapsed);
	UI_FORMAT(text, "%s  COINS %u", "%s 金币%u", time, maze_coin_count(game->run.coins));
	centered(120, 122, text, 1, INK);
	if (game->new_record) {
		UI_FORMAT(text, "NEW BEST  P%lu", "新纪录 分%lu", (unsigned long)game->run.score);
	} else if (game->elapsed <= game->level.spec->target_seconds * 1000u) {
		UI_FORMAT(text, "TARGET MET  P%lu", "达标 分%lu", (unsigned long)game->run.score);
	} else if (game->expedition != EXP_RANDOM) {
		time_text(time, game->expedition == EXP_DAILY ? adventure_daily_best(game, game->challenge_date) :
			  game->record[game->index]);
		UI_FORMAT(text, "BEST %s  P%lu", "最佳%s 分%lu", time, (unsigned long)game->run.score);
	} else {
		UI_FORMAT(text, "%u/3 STARS  P%lu", "%u/3星 分%lu", game->stars, (unsigned long)game->run.score);
	}
	centered(120, 216, text, 1, INK);
}

static void render_card(const struct game *game, unsigned int progress, unsigned int pulse)
{
	char timer[20];
	char line_text[48];

	circle(120, 120, 116, INK);
	circle(120, 120, 114, SKY);
	switch (game->page) {
	case PAGE_MENU:
		render_home();
		break;
	case PAGE_GAMES:
		centered(132, 43, "GAMES", 2, INK);
		break;
	case PAGE_WORLDS:
		centered(132, 38, "ADVENTURE", 2, INK);
		centered(132, 59, "SELECT A WORLD", 1, TEAL);
		break;
	case PAGE_SETTINGS:
		centered(132, 43, "SETTINGS", 2, INK);
		break;
	case PAGE_COUNTDOWN:
		line_text[0] = '0' + game->countdown;
		line_text[1] = '\0';
		centered(120, 77, game->mode == MODE_MAZE ? game->level.name : arcade_name(game->mode), 2, TEAL);
		ring(120, 123, 27, TEAL, COURT);
		centered(120, 109, line_text, 4, INK);
		if (game->mode == MODE_MAZE && game->level.spec->tip) {
			centered(120, 161, game->level.spec->tip, 1, SOFT);
		} else {
			centered(120, 161, game->mode == MODE_PINBALL ? "TAP TO LAUNCH" :
				 game->mode == MODE_SPACE ? "TILT TO FLY - TAP TO FIRE" : "TILT TO MOVE", 1, SOFT);
		}
		break;
	case PAGE_EXTRAS:
		centered(120, 76, "EXTRAS", 3, TEAL);
		UI_FORMAT(line_text, "WALLET %lu", "钱包 %lu", (unsigned long)game->wallet);
		centered(120, 119, line_text, 1, BRONZE);
		break;
	case PAGE_CHALLENGES: {
		uint32_t date = game->growth.date;
		uint32_t best = adventure_daily_best(game, date);

		centered(135, 43, "CHALLENGES", 2, TEAL);
		if (date) {
			UI_FORMAT(line_text, "DATE %04lu-%02lu-%02lu", "日期 %04lu-%02lu-%02lu",
				       (unsigned long)(date / 10000), (unsigned long)(date / 100 % 100),
				       (unsigned long)(date % 100));
		} else {
			(void)snprintf(line_text, sizeof(line_text), "SET A DATE FOR DAILY");
		}
		centered(120, 77, line_text, 1, INK);
		if (best) {
			time_text(timer, best);
			UI_FORMAT(line_text, "DAILY BEST %s", "每日最佳 %s", timer);
		} else {
			(void)snprintf(line_text, sizeof(line_text), "DAILY BEST -");
		}
		centered(120, 93, line_text, 1, SOFT);
		UI_FORMAT(line_text, "SEED %05lu  0 AUTO", "种子 %05lu  0自动", (unsigned long)game->seed_input);
		centered(120, 110, line_text, 1, BRONZE);
		break;
	}
	case PAGE_DATE:
	case PAGE_SEED: {
		uint32_t edit = game->edit_date;
		static const char *const fields[] = {"YEAR", "MONTH", "DAY"};

		centered(120, 51, game->page == PAGE_DATE ? "SET DATE" : "SET SEED", 2, TEAL);
		if (game->page == PAGE_DATE) {
			(void)snprintf(line_text, sizeof(line_text), "%04lu-%02lu-%02lu",
				       (unsigned long)(edit / 10000), (unsigned long)(edit / 100 % 100),
				       (unsigned long)(edit % 100));
		} else {
			(void)snprintf(line_text, sizeof(line_text), "%05lu", (unsigned long)edit);
		}
		centered(120, 80, line_text, 2, INK);
		if (game->page == PAGE_DATE) {
			centered(120, 107, fields[game->edit_field], 1, BRONZE);
		} else {
			UI_FORMAT(line_text, "DIGIT %u  -  00000 AUTO", "第%u位  00000为自动", game->edit_field + 1u);
			centered(120, 107, line_text, 1, BRONZE);
		}
		break;
	}
	case PAGE_SKINS: {
		unsigned int skin = game->collection_cursor;
		unsigned int cost = adventure_skin_cost(skin);
		bool unlocked = !skin || (game->growth.skins & MAZE_BIT(skin));
		static const char *const hints[] = {"", "EARN 12 COINS", "CLEAR FORGE BOSS",
			"", "", "CLEAR ALL 25 LEVELS"};

		centered(120, 37, "BALL SKINS", 1, SOFT);
		ring(120, 66, 21, TEAL, COURT);
		mascot(120, 66, 17, skin, FACE_HAPPY);
		centered(120, 92, adventure_skin(skin), 2, INK);
		if (unlocked) {
			centered(120, 118, skin == game->growth.skin ? "EQUIPPED" : "UNLOCKED", 1, GREEN_INK);
		} else if (cost) {
			UI_FORMAT(line_text, "%u COINS  WALLET %lu", "价格%u币  钱包%lu",
				       cost, (unsigned long)game->wallet);
			centered(120, 118, line_text, 1, BRONZE);
		} else {
			centered(120, 118, hints[skin], 1, BRONZE);
		}
		break;
	}
	case PAGE_ACHIEVEMENTS: {
		unsigned int badge = game->collection_cursor;
		static const char *const hints[] = {"CLEAR A LEVEL", "BEAT THE SPEED TARGET",
			"CLEAR WITH NO COLLISIONS", "COLLECT EVERY COIN", "SURVIVE FOR 120 SECONDS",
			"CLEAR ALL 25 LEVELS", "UNLOCK ALL SIX SKINS", "REACH A FIVE COIN COMBO"};
		bool unlocked = game->growth.achievements & MAZE_BIT(badge);

		centered(120, 40, "ACHIEVEMENTS", 1, SOFT);
		(void)snprintf(line_text, sizeof(line_text), "%u / %u", badge + 1, ACH_COUNT);
		centered(120, 62, line_text, 2, TEAL);
		centered(120, 94, adventure_achievement(badge), 2, INK);
		centered(120, 119, hints[badge], 1, SOFT);
		centered(120, 141, unlocked ? "UNLOCKED" : "LOCKED", 1, unlocked ? GREEN_INK : BRONZE);
		break;
	}
	case PAGE_ARCADE: {
		enum mode selected = game->selected_mode;
		static const char *const instructions[] = {
			"", "60 SEC - COLLECT COINS", "DODGE WITH THREE LIVES", "STAY INSIDE THE GREEN RING",
			"TILT AND TAP TO FLIP", "TILT TO FLY - TAP TO FIRE", "TILT TO STEER AND EAT",
		};
		static const char icons[] = {' ', 'C', 'D', 'B', 'P', 'S', 'T'};
		char icon[] = {icons[selected], 0};

		centered(120, 32, "ARCADE", 1, SOFT);
		ring(120, 58, 13, CYAN, COURT);
		static const char *const han_icons[] = {"", "币", "躲", "衡", "珠", "航", "蛇"};

		centered(120, 51, chinese ? han_icons[selected] : icon, 2, INK);
		centered(120, 84, arcade_name(selected), 2, TEAL);
		centered(120, 107, instructions[selected], 1, INK);
		if (selected == MODE_SNAKE) {
			UI_FORMAT(line_text, "BEST %lu PTS  LEN %u", "最高%lu分  最长%u",
				       (unsigned long)arcade_best_score(game, selected),
				       game->snake_best_length[game->settings.snake_free]);
		} else if (selected == MODE_COINS || selected >= MODE_PINBALL) {
			UI_FORMAT(line_text, "BEST %lu PTS", "最高 %lu分",
				       (unsigned long)arcade_best_score(game, selected));
		} else {
			time_text(timer, game->arcade_best[selected] * 100);
			UI_FORMAT(line_text, "BEST %s", "最佳 %s", timer);
		}
		centered(120, 123, line_text, 1, BRONZE);
		break;
	}
	case PAGE_SELECT: {
		const struct level_spec *spec = &maze_levels[game->index];

		unsigned int position = adventure_position(game->index);

		centered(120, 74, adventure_world(game->index), 2, INK);
		UI_FORMAT(line_text, "WORLD %u / %u", "世界 %u / %u",
			       position / WORLD_SIZE + 1, ADVENTURE_WORLDS);
		centered(130, 49, line_text, 1, TEAL);
		centered(120, 124, spec->name, 2, INK);
		UI_FORMAT(line_text, "LEVEL %02u/%u  STARS %u/3", "关卡 %02u/%u  星级 %u/3",
			       position + 1, MAZE_LEVELS, game->growth.stars[game->index]);
		centered(120, 143, line_text, 1, BRONZE);
		if (game->record[game->index]) {
			time_text(timer, game->record[game->index]);
			UI_FORMAT(line_text, "BEST %s", "最佳 %s", timer);
			centered(120, 156, line_text, 1, SOFT);
		} else {
			UI_FORMAT(line_text, "TARGET %u SEC", "目标 %u秒", spec->target_seconds);
			centered(120, 156, line_text, 1, SOFT);
		}
		break;
	}
	case PAGE_BRIEF:
		centered(120, 75, game->expedition == EXP_DAILY ? "DAILY RUN" :
			 game->expedition == EXP_RANDOM ? "RANDOM RUN" : game->level.name, 2, TEAL);
		centered(120, 99, game->level.spec->tip ? game->level.spec->tip : "ROLL TO THE GREEN GOAL", 1, INK);
		if (game->expedition != EXP_STORY) {
			UI_FORMAT(line_text, "SEED %lu  TIER %u", "种子%lu  难度%u",
				       (unsigned long)game->challenge_seed, game->challenge_tier + 1u);
			centered(120, 115, line_text, 1, SOFT);
		} else if (game->level.spec->hint) {
			centered(120, 115, game->level.spec->hint, 1, SOFT);
		}
		UI_FORMAT(line_text, "TARGET %u SEC", "目标 %u秒", game->level.spec->target_seconds);
		centered(120, 130, line_text, 1, BRONZE);
		break;
	case PAGE_GENERATE:
		centered(120, 96, "BUILDING MAZE", 2, TEAL);
		centered(120, 124, "CHECKING THE ROUTE", 1, INK);
		break;
	case PAGE_CALIBRATE:
		ring(120, 64, 21, CYAN, SKY);
		circle(120, 64, 5, GOLD);
		centered(120, 99, "HOLD FLAT", 2, INK);
		centered(120, 123, "CALIBRATING ZERO", 1, SOFT);
		progress_bar(149, progress);
		break;
	case PAGE_PAUSE:
		centered(120, 64, game->mode == MODE_MAZE ? game->level.name :
			 arcade_name(game->mode), 1, SOFT);
		centered(120, 86, "PAUSED", 3, INK);
		centered(120, 116, "HOLD SCREEN TO RECALIBRATE", 1, SOFT);
		break;
	case PAGE_QUIT:
		centered(120, 67, "LEAVE LEVEL", 2, INK);
		centered(120, 98, "THIS RUN WILL BE LOST", 1, RED_INK);
		centered(120, 116, "BEST SCORES ARE KEPT", 1, SOFT);
		break;
	case PAGE_WIN:
	case PAGE_FAIL:
		if (game->mode == MODE_MAZE) {
			maze_result(game, pulse);
		} else {
			arcade_result(game, pulse);
		}
		break;
	case PAGE_IMU_ERROR:
		centered(120, 72, "IMU ERROR", 2, RED_INK);
		centered(120, 101, "CHECK LEVEL AND HOLD STILL", 1, INK);
		centered(120, 120, "THEN RETRY CALIBRATION", 1, SOFT);
		break;
	default:
		break;
	}
	draw_controls(game);
	draw_corner(corner_action(game->page));
	if (game->page == PAGE_MENU || (game->mode == MODE_MAZE && (game->page == PAGE_WIN || game->page == PAGE_FAIL))) {
		round_edges();
	}
}

static int repaint(const struct game *game, struct rect damage, unsigned int progress,
		   unsigned int pulse, art_sink sink)
{
	scissor = clip(damage, (struct rect){0, 0, MAZE_WIDTH, MAZE_HEIGHT});
	if (scissor.w <= 0 || scissor.h <= 0) {
		return 0;
	}
	fill(0, 0, MAZE_WIDTH, MAZE_HEIGHT, SKY);
	if (game->page == PAGE_PLAY) {
		if (game->mode == MODE_MAZE) {
			render_maze(game, progress);
		} else if (game->mode >= MODE_PINBALL) {
			render_motion(game, progress);
		} else {
			render_arena(game, progress);
		}
	} else {
		render_card(game, progress, pulse);
	}

	if (!game->settings.rotated && scissor.w == MAZE_WIDTH) {
		return sink(0, scissor.y, scissor.w, scissor.h, frame[scissor.y]);
	}
	int batch = ARRAY_COUNT(transfer) / scissor.w;

	for (int row = 0; row < scissor.h; row += batch) {
		int rows = min(batch, scissor.h - row);
		int x = scissor.x, y = scissor.y + row;

		if (game->settings.rotated) {
			for (int idx = 0; idx < rows; idx++) {
				const uint16_t *source = &frame[y + rows - 1 - idx][x];

				for (int col = 0; col < scissor.w; col++) {
					transfer[idx * scissor.w + col] = source[scissor.w - 1 - col];
				}
			}
			x = MAZE_WIDTH - x - scissor.w;
			y = MAZE_HEIGHT - y - rows;
		} else {
			for (int idx = 0; idx < rows; idx++) {
				memcpy(transfer + idx * scissor.w, &frame[y + idx][x],
				       (size_t)scissor.w * sizeof(*transfer));
			}
		}
		int ret = sink(x, y, scissor.w, rows, transfer);

		if (ret < 0) {
			return ret;
		}
	}
	return 0;
}

static struct rect around(struct point at, int radius)
{
	return (struct rect){(int)floorf(at.x) - radius, (int)floorf(at.y) - radius,
			     radius * 2 + 2, radius * 2 + 2};
}

static struct rect span_rect(struct rect a, struct rect b)
{
	int left = min(a.x, b.x), top = min(a.y, b.y);

	return (struct rect){left, top, max(a.x + a.w, b.x + b.w) - left,
			     max(a.y + a.h, b.y + b.h) - top};
}

static void damage_add(struct rect *areas, unsigned int *count, struct rect area)
{
	for (unsigned int idx = 0; idx < *count;) {
		struct rect previous = areas[idx];
		struct rect joined = span_rect(previous, area);

		if (joined.w * joined.h <= previous.w * previous.h + area.w * area.h + 96) {
			area = joined;
			areas[idx] = areas[--*count];
			idx = 0;
		} else {
			idx++;
		}
	}
	if (*count < 24) {
		areas[(*count)++] = area;
	} else {
		areas[0] = (struct rect){0, 0, MAZE_WIDTH, MAZE_HEIGHT};
		*count = 1;
	}
}

static bool moved(struct point old, struct point current)
{
	return (int)old.x != (int)current.x || (int)old.y != (int)current.y;
}

static void remember_motion(const struct game *game, struct art_state *state)
{
	const struct arcade_run *run = &game->arcade;

	state->motion_at[0] = game->ball;
	state->motion_size[0] = 12;
	switch (game->mode) {
	case MODE_PINBALL:
		state->items = (run->pinball.loaded ? 1u : 0u) |
			       (game->elapsed < run->pinball.flip_until ? 2u : 0u);
		state->effects |= (uint16_t)run->combo << 8;
		break;
	case MODE_SPACE: {
		const struct space_run *space = &run->space;

		state->items = (space->shield ? 1u : 0u) | (space->pickup_shield ? 2u : 0u);
		state->motion_at[1] = space->pickup.at;
		state->motion_size[1] = space->pickup.radius ? 8 : 0;
		for (unsigned int idx = 0; idx < SPACE_ROCKS; idx++) {
			state->motion_at[2 + idx] = space->rocks[idx].at;
			state->motion_size[2 + idx] = space->rocks[idx].radius;
		}
		for (unsigned int idx = 0; idx < SPACE_SHOTS; idx++) {
			state->motion_at[2 + SPACE_ROCKS + idx] = space->shots[idx].at;
			state->motion_size[2 + SPACE_ROCKS + idx] = space->shots[idx].radius ? 4 : 0;
		}
		break;
	}
	case MODE_SNAKE:
		state->effects = run->snake.length;
		for (unsigned int idx = 0; idx < run->snake.length; idx++) {
			state->motion_at[idx] = run->snake.body[idx];
			state->motion_size[idx] = idx ? 4 : 5;
		}
		state->motion_at[SNAKE_SEGMENTS] = run->snake.food;
		state->motion_size[SNAKE_SEGMENTS] = 5;
		break;
	default: break;
	}
}

static void motion_damage(const struct art_state *old, const struct art_state *next, bool quiet,
			  struct rect *areas, unsigned int *count)
{
	for (unsigned int idx = 0; idx < ARRAY_COUNT(next->motion_at); idx++) {
		unsigned int before = old->motion_size[idx], after = next->motion_size[idx];

		if ((!before && !after) || (before == after && !moved(old->motion_at[idx], next->motion_at[idx]))) {
			continue;
		}
		if (before) {
			damage_add(areas, count, around(old->motion_at[idx], before));
		}
		if (after) {
			damage_add(areas, count, around(next->motion_at[idx], after));
		}
	}
	if (next->effects != old->effects || next->score != old->score || next->coins != old->coins ||
	    next->progress != old->progress) {
		damage_add(areas, count, (struct rect){57, 201, 126, 32});
	}
	if (next->mode == MODE_PINBALL) {
		if ((old->items ^ next->items) & 1) {
			damage_add(areas, count, (struct rect){68, 133, 104, 17});
		}
		if ((old->items ^ next->items) & 2) {
			damage_add(areas, count, (struct rect){84, 185, 73, 23});
		}
	} else if (next->mode == MODE_SPACE && (old->items != next->items || old->arena_immune != next->arena_immune)) {
		damage_add(areas, count, around(old->motion_at[0], 12));
		damage_add(areas, count, around(next->motion_at[0], 12));
		if (next->motion_size[1]) {
			damage_add(areas, count, around(next->motion_at[1], 8));
		}
	}
	if (!quiet && (next->elapsed != old->elapsed || next->flash_until != old->flash_until)) {
		if (old->flash_until > old->elapsed) {
			damage_add(areas, count, around(old->flash, 18));
		}
		if (next->flash_until > next->elapsed) {
			damage_add(areas, count, around(next->flash, 18));
		}
	}
}

static void arena_damage(const struct art_state *old, const struct art_state *next,
			 struct rect *areas, unsigned int *count)
{
	struct point center = {MAZE_CENTER_X, MAZE_CENTER_Y};

	if (old->ball_x != next->ball_x || old->ball_y != next->ball_y ||
	    old->arena_immune != next->arena_immune) {
		damage_add(areas, count, around((struct point){old->ball_x, old->ball_y}, 9));
		damage_add(areas, count, around((struct point){next->ball_x, next->ball_y}, 9));
	}
	for (unsigned int slot = 0; slot < next->arena_enemies; slot++) {
		bool existed = slot < old->arena_enemies;

		if (!existed || moved(old->arena_enemy[slot], next->arena_enemy[slot])) {
			if (existed) {
				damage_add(areas, count, around(old->arena_enemy[slot], ARCADE_ENEMY_RADIUS + 1));
			}
			damage_add(areas, count, around(next->arena_enemy[slot], ARCADE_ENEMY_RADIUS + 1));
		}
	}
	for (unsigned int slot = 0; slot < ARCADE_COIN_SLOTS; slot++) {
		if ((old->arena_coin_mask ^ next->arena_coin_mask) & MAZE_BIT(slot)) {
			struct point at = next->arena_coin_mask & MAZE_BIT(slot) ?
				next->arena_coin[slot] : old->arena_coin[slot];

			damage_add(areas, count, around(at, 5));
		} else if ((next->arena_coin_mask & MAZE_BIT(slot)) &&
			   (moved(old->arena_coin[slot], next->arena_coin[slot]) ||
			    ((old->items ^ next->items) & MAZE_BIT(slot)))) {
			damage_add(areas, count, around(old->arena_coin[slot], 5));
			damage_add(areas, count, around(next->arena_coin[slot], 5));
		}
	}
	if (old->arena_safe != next->arena_safe) {
		damage_add(areas, count, around(center, max(old->arena_safe, next->arena_safe) + 1));
	}
	bool changed_wind = old->arena_wind.x != next->arena_wind.x ||
			    old->arena_wind.y != next->arena_wind.y;

	if (changed_wind) {
		const struct point winds[] = {old->arena_wind, next->arena_wind};

		for (unsigned int idx = 0; idx < ARRAY_COUNT(winds); idx++) {
			if (winds[idx].x || winds[idx].y) {
				struct point at = {center.x + winds[idx].x * 74, center.y + winds[idx].y * 74};

				damage_add(areas, count, around(at, 8));
			}
		}
	}
	bool old_flash = old->flash_until > old->elapsed;
	bool next_flash = next->flash_until > next->elapsed;

	if (old_flash != next_flash || old->flash_until != next->flash_until) {
		if (old_flash) {
			damage_add(areas, count, around(old->flash, 10));
		}
		if (next_flash) {
			damage_add(areas, count, around(next->flash, 10));
		}
	}
	if (old->tenth != next->tenth) {
		damage_add(areas, count, (struct rect){94, 15, 52, 7});
	}
	if (old->score != next->score || old->effects != next->effects || old->coins != next->coins ||
	    old->progress != next->progress || changed_wind) {
		damage_add(areas, count, (struct rect){65, 201, 110, 25});
	}
}

int art_present(struct art_state *state, const struct game *game, unsigned int progress,
		unsigned int pulse, art_sink sink)
{
	chinese = game->chinese;
	if (game->settings.quiet) {
		pulse = 0;
	}
	struct art_state next = {
		.page = game->page, .index = game->index,
		.ball_x = (int)(game->ball.x + 0.5f), .ball_y = (int)(game->ball.y + 0.5f),
		.tenth = game->elapsed / 100, .progress = progress, .pulse = pulse, .valid = true,
		.serial = game->serial, .elapsed = game->elapsed, .coins = game->run.coins,
		.items = game->run.items, .keys = game->run.keys, .skin = game->growth.skin,
		.effects = effects(game), .flash = game->run.flash, .flash_until = game->run.flash_until,
		.score = game->run.score,
		.mode = game->mode, .selected_mode = game->selected_mode,
		.growth_revision = game->growth_revision, .edit_value = game->edit_date,
		.edit_field = game->edit_field, .collection_cursor = game->collection_cursor,
		.rotated = game->settings.rotated, .countdown = game->countdown,
		.chinese = game->chinese,
		.surprised = game->run.react_until > game->elapsed,
	};
	if (game->page == PAGE_PLAY && game->mode == MODE_MAZE) {
		for (unsigned int idx = 0; idx < game->level.spec->feature_count; idx++) {
			struct feature feature = maze_feature(&game->level, idx);

			if (feature.kind == MECH_TIMED && maze_gate_active(game, &feature)) {
				next.timed_gates |= MAZE_BIT(idx);
			}
		}
	}
	if (game->page == PAGE_PLAY && game->mode != MODE_MAZE) {
		const struct arcade_run *arena = &game->arcade;

		next.arena_enemies = arena->enemies_active;
		next.arena_safe = arena->safe_radius;
		next.arena_wind = arena->wind;
		next.arena_immune = arena->immune_until > game->elapsed;
		next.score = game->mode == MODE_BALANCE ? 0 : arena->score;
		next.coins = arena->collected;
		next.effects = game->mode == MODE_COINS ? arena->combo : arena->lives;
		next.items = 0;
		next.flash = arena->flash;
		next.flash_until = arena->flash_until;
		if (game->mode == MODE_COINS) {
			next.tenth = game->elapsed < ARCADE_COIN_MS ?
				(ARCADE_COIN_MS - game->elapsed) / 100 : 0;
		}
		if (game->mode >= MODE_PINBALL) {
			remember_motion(game, &next);
		}
		for (unsigned int slot = 0; slot < arena->enemies_active; slot++) {
			next.arena_enemy[slot] = arena->enemies[slot].at;
		}
		if (game->mode == MODE_COINS) {
			for (unsigned int slot = 0; slot < ARCADE_COIN_SLOTS; slot++) {
				const struct arcade_coin *coin = &arena->coins[slot];

				if (!coin->respawn) {
					next.arena_coin_mask |= MAZE_BIT(slot);
					next.arena_coin[slot] = coin->at;
					next.items |= coin->bonus ? MAZE_BIT(slot) : 0;
				}
			}
		}
	}
	int ret = 0;

	if (!state->valid || next.page != state->page || next.index != state->index ||
	    next.serial != state->serial || next.skin != state->skin ||
	    next.mode != state->mode || next.selected_mode != state->selected_mode || next.rotated != state->rotated ||
	    next.chinese != state->chinese ||
	    (next.page != PAGE_PLAY && (next.growth_revision != state->growth_revision ||
	     next.edit_value != state->edit_value || next.edit_field != state->edit_field ||
	     next.collection_cursor != state->collection_cursor))) {
		ret = repaint(game, (struct rect){0, 0, MAZE_WIDTH, MAZE_HEIGHT}, progress, pulse, sink);
	} else if (next.page == PAGE_PLAY && game->mode != MODE_MAZE) {
		struct rect areas[24];
		unsigned int count = 0;

		if (game->mode >= MODE_PINBALL) {
			motion_damage(state, &next, game->settings.quiet, areas, &count);
		} else {
			arena_damage(state, &next, areas, &count);
		}
		for (unsigned int idx = 0; idx < count && ret == 0; idx++) {
			ret = repaint(game, areas[idx], progress, pulse, sink);
		}
	} else if (next.page == PAGE_PLAY) {
		const struct level_spec *spec = game->level.spec;
		struct rect areas[24];
		unsigned int count = 0;
		int light = lantern(game);
		int radius = light ? light + 2 : 9;

		if (next.ball_x != state->ball_x || next.ball_y != state->ball_y ||
		    next.surprised != state->surprised || ((next.effects ^ state->effects) & 0x100)) {
			damage_add(areas, &count, around((struct point){state->ball_x, state->ball_y}, radius));
			damage_add(areas, &count, around((struct point){next.ball_x, next.ball_y}, radius));
		}
		if (next.elapsed != state->elapsed) {
			for (unsigned int idx = 0; idx < spec->feature_count; idx++) {
				struct feature configured = maze_feature(&game->level, idx);
				const struct feature *feature = &configured;
				float previous_time = maze_environment_time(game, state->elapsed);
				float current_time = maze_environment_time(game, next.elapsed);

				if (!maze_feature_on(game, feature)) {
					continue;
				}
				if (feature->kind == MECH_MOVING) {
					int pad = feature->radius + 2;

					damage_add(areas, &count, around(maze_mover(feature, previous_time), pad));
					damage_add(areas, &count, around(maze_mover(feature, current_time), pad));
				} else if (feature->kind == MECH_ROTOR) {
					struct wall old = maze_rotor(feature, previous_time);
					struct wall current = maze_rotor(feature, current_time);

					damage_add(areas, &count, span_rect(around(old.start, 4), around(old.end, 4)));
					damage_add(areas, &count,
						   span_rect(around(current.start, 4), around(current.end, 4)));
				}
			}
		}
		if (next.tenth != state->tenth) {
			damage_add(areas, &count, (struct rect){102, 16, 49, 7});
		}
		if (next.progress != state->progress ||
		    (game->run.spawned && (next.effects != state->effects || next.score != state->score ||
		     (next.flash_until > next.elapsed) != (state->flash_until > state->elapsed)))) {
			damage_add(areas, &count, (struct rect){65, 201, 110, 35});
		}
		for (unsigned int idx = 0; idx < spec->coin_count; idx++) {
			if ((next.coins ^ state->coins) & MAZE_BIT(idx)) {
				damage_add(areas, &count, around(maze_transform(&game->level, spec->coins[idx]), 5));
			}
		}
		for (unsigned int idx = 0; idx < spec->item_count; idx++) {
			if ((next.items ^ state->items) & MAZE_BIT(idx)) {
				damage_add(areas, &count, around(maze_item(&game->level, idx).at, 8));
			}
		}
		for (unsigned int idx = 0; idx < spec->feature_count; idx++) {
			struct feature configured = maze_feature(&game->level, idx);
			const struct feature *feature = &configured;

			if (feature->kind == MECH_TIMED && ((next.timed_gates ^ state->timed_gates) & MAZE_BIT(idx))) {
				damage_add(areas, &count, span_rect(around(feature->a, 5), around(feature->b, 5)));
			}

			if (feature->kind == MECH_KEY && ((next.keys ^ state->keys) & MAZE_BIT(feature->id))) {
				struct rect area = around(feature->a, 8);

				if (feature->door) {
					area = span_rect(area, around(feature->b, 4));
				}
				damage_add(areas, &count, area);
			}
		}
		if (next.elapsed != state->elapsed || next.flash_until != state->flash_until) {
			if (state->flash_until > state->elapsed) {
				damage_add(areas, &count, around(state->flash, 13));
			}
			if (next.flash_until > next.elapsed) {
				damage_add(areas, &count, around(next.flash, 13));
			}
		}
		for (unsigned int idx = 0; idx < count && ret == 0; idx++) {
			ret = repaint(game, areas[idx], progress, pulse, sink);
		}
	} else if (next.page == PAGE_CALIBRATE && next.progress != state->progress) {
		ret = repaint(game, (struct rect){72, 149, 96, 7}, progress, pulse, sink);
	} else if (next.page == PAGE_COUNTDOWN && next.countdown != state->countdown) {
		ret = repaint(game, (struct rect){96, 106, 48, 34}, progress, pulse, sink);
	} else if ((next.page == PAGE_WIN || next.page == PAGE_FAIL) &&
		   next.pulse / 3 != state->pulse / 3 && game->mode == MODE_MAZE) {
		ret = repaint(game, (struct rect){55, 17, 130, 63}, progress, pulse, sink);
	} else if ((next.page == PAGE_WIN || next.page == PAGE_FAIL) &&
		   game->mode != MODE_MAZE && next.pulse != state->pulse) {
		ret = repaint(game, (struct rect){108, 28, 25, 25}, progress, pulse, sink);
	}

	if (ret == 0) {
		*state = next;
	} else {
		state->valid = false;
	}
	return ret;
}
