#pragma once

#include <stdbool.h>

#define FLAPPY_WORLD_W 300
#define FLAPPY_WORLD_H 166
#define FLAPPY_BIRD_X 52
#define FLAPPY_BIRD_W 18
#define FLAPPY_BIRD_H 14
#define FLAPPY_PIPE_W 34
#define FLAPPY_GAP_H 68
#define FLAPPY_PIPE_COUNT 3

typedef struct {
    int bird_y;
    int bird_vy;
    int pipe_x[FLAPPY_PIPE_COUNT];
    int gap_y[FLAPPY_PIPE_COUNT];
    bool pipe_scored[FLAPPY_PIPE_COUNT];
    int score;
    bool started;
    bool game_over;
} flappy_game_t;

void flappy_init(flappy_game_t *g);
void flappy_flap(flappy_game_t *g);
void flappy_step(flappy_game_t *g);
