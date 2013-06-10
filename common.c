#include "common.h"

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>

bool username_is_valid(const char *username, uint8_t length) {
	int i;
	
	assert(username != NULL);
	
	if ( length < MIN_USERNAME_LENGTH ) return FALSE;
	if ( length > MAX_USERNAME_LENGTH ) return FALSE;
	assert(username[length] == '\0');
	
	for ( i = 0; i < length; i++ )
		if ( strchr(USERNAME_ALPHABET, username[i]) == NULL )
			return FALSE;
	
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
		case ZOMBIE:    return "ZOMBIE";
	}
	
	assert(FALSE);
	return NULL; /* never executed */
}

uint8_t state_encode(enum client_state state) {
	switch ( state ) {
		case NONE:      return 'N';
		case CONNECTED: return 'C';
		case FREE:      return 'F';
		case BUSY:      return 'B';
		case PLAY:      return 'P';
		case ZOMBIE:	return 'Z';
	}
	
	assert(FALSE);
	return '\0'; /* never executed */
}

enum client_state state_decode(uint8_t enc) {
	switch ( enc ) {
		case 'N': return NONE;
		case 'C': return CONNECTED;
		case 'F': return FREE;
		case 'B': return BUSY;
		case 'P': return PLAY;
		case 'Z': return ZOMBIE;
	}
	
	assert(FALSE);
	return NONE; /* never executed */
}

int get_line(char *buffer, int size) {
	int length;
	
	assert(size > 0);
	
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

int pack(void *buffer, const char *format, ...) {
	va_list args;
	char *buff = (char*) buffer;
	uint16_t _word;
	uint32_t _long;
	char *_str;

	va_start(args, format);
	
	for ( ; *format != '\0'; format++ ) {
		switch (*format) {
			case 'b':
				*buff = (uint8_t) va_arg(args, int);
				buff++;
				break;
			
			case 'w':
				_word = (uint16_t) va_arg(args, int);
				_word = htons(_word);
				memcpy(buff, &_word, 2);
				buff += 2;
				break;
			
			case 'l':
				_long = (uint32_t) va_arg(args, int);
				_long = htonl(_long);
				memcpy(buff, &_long, 4);
				buff += 4;
				break;
			
			case 's':
			case 'S':
				_str = va_arg(args, char*);
				for (; *_str != '\0'; _str++, buff++)
					*buff = *_str;
				if (*format == 'S') *(buff++) = '\0';
		}
	}
	
	va_end(args);
	
	return buff - (char *) buffer;
}

void unpack(const void *buffer, const char *format, ...) {
	va_list args;
	char *buff = (char*) buffer;
	uint16_t _word;
	uint32_t _long;
	char *_str;
	int length;
	
	va_start(args, format);
	
	for ( ; *format != '\0'; format++ ) {
		switch ( *format ) {
			case 'b':
				*(va_arg(args, uint8_t*)) = *buff;
				buff++;
				break;
			
			case 'w':
				memcpy(&_word, buff, 2);
				_word = ntohs(_word);
				*(va_arg(args, uint16_t*)) = _word;
				buff += 2;
				break;
			
			case 'l':
				memcpy(&_long, buff, 4);
				_long = ntohl(_long);
				*(va_arg(args, uint32_t*)) = _long;
				buff += 4;
				break;
			
			case 's':
				length = va_arg(args, int);
				_str = va_arg(args, char*);
				memcpy(_str, buff, length);
				_str[length] = '\0';
				buff += length;
		}
	}
	
	va_end(args);
}
