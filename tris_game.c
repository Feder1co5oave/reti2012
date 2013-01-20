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

char get_winner(const struct tris_grid *grid) {
	char players[] = {GAME_HOST, GAME_GUEST};
	int h, i, c = 0;
	
	for ( i = 1; i <= 9; i++ ) {
		if ( grid->cells[i] == GAME_HOST || grid->cells[i] == GAME_GUEST) c++;
		else assert(grid->cells[i] == GAME_UNDEF);
	}
	
	for ( h = 0; h < 2; h++ ) {
		for ( i = 0; i < 8; i++ ) {
			if ( grid->cells[ win[i][0] ] == players[h] &&
                                       grid->cells[ win[i][1] ] == players[h] &&
                                      grid->cells[ win[i][2] ] == players[h] ) {
				
				return players[h];
			}
		}
	}
	
	if ( c < 9 ) return GAME_UNDEF;
	else return GAME_DRAW;
}

char inverse(char player) {
	switch ( player ) {
		case GAME_HOST: return GAME_GUEST;
		case GAME_GUEST: return GAME_HOST;
		default: return player;
	}
}

char *sprintgrid(char *buffer, const struct tris_grid *grid, const char *pre,
                                                                     size_t n) {
	
	int i, j;
	char temp[5];
	buffer[0] = '\0';
	
	if ( pre == NULL ) pre = "";
	
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
