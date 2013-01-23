#include "common.h"

#include <string.h>
#include <stdio.h>
#include <sys/socket.h>

bool username_is_valid(const char *username, uint8_t length) {
	int i;
	if ( length < MIN_USERNAME_LENGTH ) return FALSE;
	if ( length > MAX_USERNAME_LENGTH ) return FALSE;
	if ( username[length] != '\0' ) return FALSE;
	
	for ( i = 0; i < length; i++ ) {
		if ( strchr(USERNAME_ALPHABET, username[i]) == NULL )
			return FALSE;
	}
	
	return TRUE;
}

char magic_name_buffer[10];

const char *magic_name(uint8_t value) {
	switch ( value ) {
		case REQ_END:       return "REQ_END";
		case REQ_HELLO:     return "REQ_HELLO";
		case REQ_HIT:       return "REQ_HIT";
		case REQ_LOGIN:     return "REQ_LOGIN";
		case REQ_PLAY:      return "REQ_PLAY";
		case REQ_WHO:       return "REQ_WHO";
		case RESP_BADREQ:   return "RESP_BADREQ";
		case RESP_BADUSR:   return "RESP_BADUSR";
		case RESP_BUSY:     return "RESP_BUSY";
		case RESP_EXIST:    return "RESP_EXIST";
		case RESP_NONEXIST: return "RESP_NONEXIST";
		case RESP_OK_FREE:  return "RESP_OK_FREE";
		case RESP_OK_LOGIN: return "RESP_OK_LOGIN";
		case RESP_OK_PLAY:  return "RESP_OK_PLAY";
		case RESP_REFUSE:   return "RESP_REFUSE";
		case RESP_WHO:      return "RESP_WHO";
		default:
			sprintf(magic_name_buffer, "0x%02hx", (uint16_t) value);
			return magic_name_buffer;
	}
}

const char *state_name(enum client_state state) {
	switch ( state ) {
		case NONE:      return "NONE";
		case CONNECTED: return "CONNECTED";
		case FREE:      return "FREE";
		case BUSY:      return "BUSY";
		case PLAY:      return "PLAY";
	}

	return NULL; /* never executed */
}

int get_line(char *buffer, int size) {
	int length;
	
	fgets(buffer, size, stdin);
	length = strlen(buffer) - 1;
	buffer[length] = '\0';
	return length;
}

int send_buffer(int socket, const char *buffer, int length) {
	int sent, counter = 0;
	
	while ( length > counter ) {
		sent = send(socket, buffer + counter, length - counter, 0);
		if ( sent < 0 ) return sent;
		counter += sent;
	}
	
	return 0;
}

int send_byte(int socket, uint8_t byte) {
	return send(socket, &byte, 1, 0);
}
