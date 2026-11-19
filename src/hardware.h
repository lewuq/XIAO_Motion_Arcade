#ifndef TILT_MAZE_HARDWARE_H
#define TILT_MAZE_HARDWARE_H

#include "maze.h"

struct touch {
	int x, y;
};

int panel_init(void);
int panel_show(void);
void panel_brightness(unsigned int level);
int panel_write(int x, int y, int width, int height, const uint16_t *pixels);
int imu_read(float accel[3]);
int touch_prepare(void);
int touch_init(void);
int touch_read(struct touch *point);

#endif
