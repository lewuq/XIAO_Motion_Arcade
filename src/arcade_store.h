#ifndef TILT_MAZE_ARCADE_STORE_H
#define TILT_MAZE_ARCADE_STORE_H

struct game;

void maze_save_load(struct game *game);
void maze_save_queue(const struct game *game);

#endif
