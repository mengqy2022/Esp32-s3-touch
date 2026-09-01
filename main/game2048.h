#pragma once
#include <stdbool.h>
typedef struct { int tile[4][4]; int score; } game2048_t;
void game2048_init(game2048_t *g);
bool game2048_move(game2048_t *g, int dir); // 0 up 1 down 2 left 3 right
