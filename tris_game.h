#ifndef _TRIS_GAME_H
#define _TRIS_GAME_H

#include "common.h"
#include <string.h>



/* ===[ Constants ]========================================================== */

#define GAME_HOST  'X'
#define GAME_GUEST 'O'
#define GAME_DRAW  'D'
#define GAME_UNDEF ' '

#define TRIS_GRID_MAP_INIT {{'\0', '1', '2', '3', '4', '5', '6', '7', '8', '9'}}

#define TRIS_GRID_INIT {{'\0', GAME_UNDEF, GAME_UNDEF, GAME_UNDEF, GAME_UNDEF,\
                    GAME_UNDEF, GAME_UNDEF, GAME_UNDEF, GAME_UNDEF, GAME_UNDEF}}



/* ===[ Data types ]========================================================= */

struct tris_grid {
	char cells[10];
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

/* ========================================================================== */

#endif
