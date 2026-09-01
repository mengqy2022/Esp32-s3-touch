#include "flappy.h"

#include <stdlib.h>
#include <string.h>

static int new_gap_y(void)
{
    const int top_margin = 18;
    const int bottom_margin = 18;
    const int usable = FLAPPY_WORLD_H - FLAPPY_GAP_H - top_margin - bottom_margin;
    return top_margin + (usable > 0 ? rand() % (usable + 1) : 0);
}

void flappy_init(flappy_game_t *g)
{
    if (!g) return;
    memset(g, 0, sizeof(*g));
    g->bird_y = FLAPPY_WORLD_H / 2 - FLAPPY_BIRD_H / 2;
    g->bird_vy = 0;
    for (int i = 0; i < FLAPPY_PIPE_COUNT; ++i) {
        g->pipe_x[i] = 180 + i * 112;
        g->gap_y[i] = new_gap_y();
        g->pipe_scored[i] = false;
    }
}

void flappy_flap(flappy_game_t *g)
{
    if (!g || g->game_over) return;
    g->started = true;
    g->bird_vy = -6;
}

static bool rect_overlap(int ax, int ay, int aw, int ah,
                         int bx, int by, int bw, int bh)
{
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

void flappy_step(flappy_game_t *g)
{
    if (!g || !g->started || g->game_over) return;

    if (g->bird_vy < 8) g->bird_vy += 1;
    g->bird_y += g->bird_vy;

    if (g->bird_y < 0 || g->bird_y + FLAPPY_BIRD_H >= FLAPPY_WORLD_H) {
        g->game_over = true;
        return;
    }

    int rightmost = 0;
    for (int i = 0; i < FLAPPY_PIPE_COUNT; ++i) {
        if (g->pipe_x[i] > rightmost) rightmost = g->pipe_x[i];
    }

    for (int i = 0; i < FLAPPY_PIPE_COUNT; ++i) {
        g->pipe_x[i] -= 3;

        if (!g->pipe_scored[i] && g->pipe_x[i] + FLAPPY_PIPE_W < FLAPPY_BIRD_X) {
            g->pipe_scored[i] = true;
            g->score++;
        }

        if (g->pipe_x[i] + FLAPPY_PIPE_W < 0) {
            int max_x = rightmost;
            for (int j = 0; j < FLAPPY_PIPE_COUNT; ++j) {
                if (g->pipe_x[j] > max_x) max_x = g->pipe_x[j];
            }
            g->pipe_x[i] = max_x + 112;
            g->gap_y[i] = new_gap_y();
            g->pipe_scored[i] = false;
            rightmost = g->pipe_x[i];
        }

        const int px = g->pipe_x[i];
        const int gap_top = g->gap_y[i];
        const int gap_bottom = gap_top + FLAPPY_GAP_H;

        bool hit_top = rect_overlap(FLAPPY_BIRD_X, g->bird_y,
                                    FLAPPY_BIRD_W, FLAPPY_BIRD_H,
                                    px, 0, FLAPPY_PIPE_W, gap_top);
        bool hit_bottom = rect_overlap(FLAPPY_BIRD_X, g->bird_y,
                                       FLAPPY_BIRD_W, FLAPPY_BIRD_H,
                                       px, gap_bottom, FLAPPY_PIPE_W,
                                       FLAPPY_WORLD_H - gap_bottom);
        if (hit_top || hit_bottom) {
            g->game_over = true;
            return;
        }
    }
}
