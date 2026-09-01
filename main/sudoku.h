#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SUDOKU_N       9
#define SUDOKU_PUZZLES 3

typedef struct {
    uint8_t grid[SUDOKU_N][SUDOKU_N];  // current board state, 0 = empty
    uint8_t given[SUDOKU_N][SUDOKU_N]; // 1 = clue (not editable), 0 = editable
    int sel_x, sel_y;                  // selected cell
} sudoku_t;

// Load puzzle #idx into s.
void sudoku_new(sudoku_t *s, int idx);

// True if the cell is a given clue (not editable).
bool sudoku_is_given(const sudoku_t *s, int x, int y);

// Set cell value (ignored if it is a clue). Returns true if it causes a conflict.
bool sudoku_set(sudoku_t *s, int x, int y, int val);

// Clear an editable cell.
void sudoku_clear(sudoku_t *s, int x, int y);

// Does this cell currently conflict with its row/column/box?
bool sudoku_has_conflict(const sudoku_t *s, int x, int y);

// True when the board is fully filled with no conflicts.
bool sudoku_is_complete(const sudoku_t *s);

#ifdef __cplusplus
}
#endif
