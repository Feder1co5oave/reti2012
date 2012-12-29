#include "common.h"

#include <string.h>
#include <stdio.h>

bool username_is_valid(const char *username) {
	int i, length = strlen(username);
	if (length < 3 || length > MAX_USERNAME_LENGTH) return FALSE;
	
	for ( i = 0; i < length; i++ ) {
		if ( strchr(USERNAME_ALPHABET, username[i]) == NULL )
			return FALSE;
	}
	
	return TRUE;
}

char magic_name_buffer[10];

const char *magic_name(uint8_t value) {
	switch ( value ) {
		case REQ_LOGIN:		return "REQ_LOGIN";
		case REQ_WHO:		return "REQ_WHO";
		case REQ_END:		return "REQ_END";
		case REQ_PLAY:		return "REQ_PLAY";
		case RESP_OK_LOGIN:	return "RESP_OK_LOGIN";
		case RESP_EXIST:	return "RESP_EXIST";
		case RESP_BADUSR:	return "RESP_BADUSR";
		case RESP_WHO:		return "RESP_WHO";
		case RESP_BUSY:		return "RESP_BUSY";
		case RESP_NONEXIST:	return "RESP_NONEXIST";
		case RESP_REFUSE:	return "RESP_REFUSE";
		case RESP_OK_PLAY:	return "RESP_OK_PLAY";
		case RESP_BADREQ:	return "RESP_BADREQ";
		default:
			sprintf(magic_name_buffer, "0x%02hx", (uint16_t) value);
			return magic_name_buffer;
	}
}

const char *state_name(enum client_state state) {
	switch ( state ) {
		case NONE:		return "NONE";
		case CONNECTED:	return "CONNECTED";
		case FREE:		return "FREE";
		case BUSY:		return "BUSY";
		case PLAY:		return "PLAY";
	}

	return NULL; /* never executed */
}
