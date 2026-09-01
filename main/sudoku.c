#include "sudoku.h"

#include <string.h>

// Three built-in puzzles (0 = empty cell). Read as 9 rows of 9 digits.
static const char *PUZZLES[SUDOKU_PUZZLES] = {
    // Easy
    "530070000"
    "600195000"
    "098000060"
    "800060003"
    "400803001"
    "700020006"
    "060000280"
    "000419005"
    "000080079",
    // Medium
    "000260701"
    "680070090"
    "190004500"
    "820100040"
    "004602900"
    "050003028"
    "009300074"
    "040050036"
    "703018000",
    // Hard
    "100489006"
    "730000040"
    "000001295"
    "007120600"
    "500703008"
    "006095700"
    "913200000"
    "060000037"
    "300568004",
};

void sudoku_new(sudoku_t *s, int idx)
{
    if (!s) return;
    if (idx < 0) idx = 0;
    if (idx >= SUDOKU_PUZZLES) idx = SUDOKU_PUZZLES - 1;

    memset(s->grid, 0, sizeof(s->grid));
    memset(s->given, 0, sizeof(s->given));
    s->sel_x = 0;
    s->sel_y = 0;

    const char *p = PUZZLES[idx];
    for (int y = 0; y < SUDOKU_N; ++y) {
        for (int x = 0; x < SUDOKU_N; ++x) {
            char c = p[y * SUDOKU_N + x];
            if (c >= '1' && c <= '9') {
                s->grid[y][x] = (uint8_t)(c - '0');
                s->given[y][x] = 1;
            }
        }
    }
}

bool sudoku_is_given(const sudoku_t *s, int x, int y)
{
    return s && x >= 0 && x < SUDOKU_N && y >= 0 && y < SUDOKU_N && s->given[y][x] != 0;
}

bool sudoku_has_conflict(const sudoku_t *s, int x, int y)
{
    if (!s) return false;
    int v = s->grid[y][x];
    if (v == 0) return false;

    // Row
    for (int i = 0; i < SUDOKU_N; ++i) {
        if (i != x && s->grid[y][i] == v) return true;
    }
    // Column
    for (int i = 0; i < SUDOKU_N; ++i) {
        if (i != y && s->grid[i][x] == v) return true;
    }
    // Box
    int bx = (x / 3) * 3, by = (y / 3) * 3;
    for (int dy = 0; dy < 3; ++dy) {
        for (int dx = 0; dx < 3; ++dx) {
            int cx = bx + dx, cy = by + dy;
            if ((cx != x || cy != y) && s->grid[cy][cx] == v) return true;
        }
    }
    return false;
}

bool sudoku_set(sudoku_t *s, int x, int y, int val)
{
    if (!s) return false;
    if (x < 0 || x >= SUDOKU_N || y < 0 || y >= SUDOKU_N) return false;
    if (sudoku_is_given(s, x, y)) return sudoku_has_conflict(s, x, y);
    if (val < 1 || val > 9) return sudoku_has_conflict(s, x, y);
    s->grid[y][x] = (uint8_t)val;
    return sudoku_has_conflict(s, x, y);
}

void sudoku_clear(sudoku_t *s, int x, int y)
{
    if (!s) return;
    if (sudoku_is_given(s, x, y)) return;
    s->grid[y][x] = 0;
}

bool sudoku_is_complete(const sudoku_t *s)
{
    if (!s) return false;
    for (int y = 0; y < SUDOKU_N; ++y) {
        for (int x = 0; x < SUDOKU_N; ++x) {
            if (s->grid[y][x] == 0) return false;
            if (sudoku_has_conflict(s, x, y)) return false;
        }
    }
    return true;
}
