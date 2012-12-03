#include "common.h"

#include <string.h>

bool is_valid_username(const char *username) {
	int length = strlen(username);
	if (length < 3 || length > MAX_USERNAME_LENGTH) return FALSE;
	
	return TRUE;
}
