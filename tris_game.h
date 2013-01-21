#ifndef _TRIS_GAME_H
#define _TRIS_GAME_H

#include "common.h"
#include <string.h>



/* ===[ Constants ]========================================================== */

#define GAME_HOST  'X'
#define GAME_GUEST 'O'
#define GAME_DRAW  'D'
#define GAME_UNDEF ' '

#define TRIS_GRID_MAP_INIT \
               {{'\0', '1', '2', '3', '4', '5', '6', '7', '8', '9'}, 0xc66b58c5}

#define TRIS_GRID_INIT {{'\0', GAME_UNDEF, GAME_UNDEF, GAME_UNDEF, GAME_UNDEF,\
        GAME_UNDEF, GAME_UNDEF, GAME_UNDEF, GAME_UNDEF, GAME_UNDEF}, 0x92fd0413}



/* ===[ Data types ]========================================================= */

struct tris_grid {
	char cells[10];
	uint32_t hash;
};



/* ===[ Functions ]========================================================== */

/**
 * Return the player who won the grid, GAME_DRAW if draw or GAME_UNDEF if there
 * is no winner and the grid is not complete.
 * @param const struct tris_grid *grid
 * @return char
 */
char get_winner(const struct tris_grid*);

/**
 * @param char a first comparison item
 * @param char b secondo comparison item
 * @param char player GAME_HOST or GAME_GUEST
 * @return TRUE if a is better than b, for player, FALSE otherwise
 */
bool better_for(char a, char b, char player);

/**
 * @param char player
 * @return char GAME_HOST if player == GAME_GUEST or GAME_GUEST if player ==
 * GAME_HOST. Return player otherwise.
 */
char inverse(char player);

/**
 * Write a graphical representation of grid into buffer, preceding any line with
 * pre, up to n bytes (including trailing \0).
 * @param char *buffer the destination buffer
 * @param const struct tris_grid *grid
 * @param const char *pre the prefix
 * @param size_t n the maximum number of bytes to write (NOT IMPLEMENTED)
 */
char *sprintgrid(char *buffer, const struct tris_grid *grid, const char *pre,
                                                                      size_t n);

uint32_t jenkins1(const char *data, size_t length);

void update_hash(struct tris_grid*);

/**
 * Compute the optimal move to make on grid, by player.
 * @param const struct tris_grid *grid the grid
 * @param char player GAME_HOST or GAME_GUEST
 * @param int *mode returns the best move to make
 * @return char the best result obtainable by player
 */
char backtrack(const struct tris_grid *grid, char player, int *move);

char evaluate(const struct tris_grid *grid, char player);

/* ========================================================================== */

#endif
