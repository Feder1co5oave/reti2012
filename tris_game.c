#include "tris_game.h"

#include <stdio.h>
#include <assert.h>

const int win[][3] = {
	{1, 2, 3},
	{4, 5, 6},
	{7, 8, 9},
	{1, 4, 7},
	{2, 5, 8},
	{3, 6, 9},
	{1, 5, 9},
	{3, 5, 7}
};

const int print[][3] = {
	{7, 8, 9},
	{4, 5, 6},
	{1, 2, 3}
};

void init_grid(struct tris_grid *grid) {
	struct tris_grid copy = TRIS_GRID_INIT;
	memcpy(grid, &copy, sizeof(struct tris_grid));
}

char get_winner(const struct tris_grid *grid) {
	int i, c = 0;
	
	for ( i = 1; i <= 9; i++ ) {
		if ( grid->cells[i] == GAME_HOST || grid->cells[i] == GAME_GUEST) c++;
		else assert(grid->cells[i] == GAME_UNDEF);
	}
	
	for ( i = 0; i < 8; i++ ) {
		if ( grid->cells[ win[i][0] ] == GAME_HOST &&
		     grid->cells[ win[i][1] ] == GAME_HOST &&
		     grid->cells[ win[i][2] ] == GAME_HOST ) return GAME_HOST;
		if ( grid->cells[ win[i][0] ] == GAME_GUEST &&
		     grid->cells[ win[i][1] ] == GAME_GUEST &&
		     grid->cells[ win[i][2] ] == GAME_GUEST ) return GAME_GUEST;
	}
	
	if ( c < 9 ) return GAME_UNDEF;
	else return GAME_DRAW;
}

char inverse(char player) {
	switch ( player ) {
		case GAME_HOST:  return GAME_GUEST;
		case GAME_GUEST: return GAME_HOST;
		default: return player;
	}
}

char *sprintgrid(char *buffer, const struct tris_grid *grid, const char *pre,
                                                                     size_t n) {
	
	int i, j;
	char temp[5];
	
	assert(buffer != NULL);
	assert(grid != NULL);
	
	if ( pre == NULL ) pre = "";
	if ( 7*(strlen(pre) + 13) + 6 + 1 > n ) return NULL;
	
	buffer[0] = '\0';
	
	for ( i = 0; i < 3; i++ ) {
		strcat(buffer, pre);
		strcat(buffer, "+---+---+---+\n");
		strcat(buffer, pre);
		for ( j = 0; j < 3; j++ ) {
			sprintf(temp, "| %c ", grid->cells[ print[i][j] ]);
			strcat(buffer, temp);
		}
		strcat(buffer, "|\n");
	}
	
	strcat(buffer, pre);
	strcat(buffer, "+---+---+---+");
	
	return buffer;
}

/**
 * A seeded variant of Jenkins's One-at-a-time hash function, which can be found
 * at http://www.burtleburtle.net/bob/hash/doobs.html#one
 */
uint32_t jenkins1(const char *data, size_t length, uint32_t h) {
	uint32_t i;
	
	for ( i = 0; i < length; i++ ) {
		h += data[i];
		h += (h << 10);
		h ^= (h >> 6);
	}
	
	h += (h << 3);
	h ^= (h >> 11);
	h += (h << 15);
	
	return h;
}

void update_hash(struct tris_grid *grid) {
	grid->hash ^= jenkins1(grid->cells, 10, grid->seed ^ grid->hash);
}
