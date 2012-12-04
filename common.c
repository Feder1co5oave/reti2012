#include "common.h"

#include <string.h>

bool username_is_valid(const char *username) {
	int i, length = strlen(username);
	if (length < 3 || length > MAX_USERNAME_LENGTH) return FALSE;
	
	for ( i = 0; i < length; i++ ) {
		if ( strchr(USERNAME_ALPHABET, username[i]) == NULL )
			return FALSE;
	}
	
	return TRUE;
}
