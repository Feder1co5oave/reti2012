#ifndef _COMMON_H
#define _COMMON_H

#include <sys/types.h>
#include <stdint.h>



/* ===[ Constants ]========================================================== */

#define MIN_USERNAME_LENGTH 3
#define MAX_USERNAME_LENGTH 30
#define USERNAME_ALPHABET \
             "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-"
#define DEFAULT_TIMEOUT_INIT {60, 0}

#define BUFFER_SIZE 1024



/* ===[ Magic constants ]==================================================== */

#define REQ_LOGIN		0x07
#define REQ_WHO 		0x62
#define REQ_END			0x99
#define REQ_PLAY		0x26

#define RESP_OK_LOGIN	0x94
#define RESP_EXIST		0x49
#define RESP_BADUSR		0x24

#define RESP_WHO		0x36

#define RESP_BUSY		0x41
#define RESP_NONEXIST	0x83
#define RESP_REFUSE		0x47

#define RESP_OK_PLAY 	0x16

#define RESP_BADREQ		0x54



/* ===[ Data types ]========================================================= */

/**
 * Describes a client state by the viewpoint of the server.
 */
enum client_state {
	NONE,       /* default value, client does not exist yet                   */
	CONNECTED,  /* client is connected, not logged in yet                     */
	FREE,       /* client is connected and logged in (username and udp port)  */
	BUSY,		/* client has/is requested to play a match                    */
	PLAY        /* client is playing                                          */
};

typedef unsigned char bool;
#define TRUE 1
#define FALSE 0



/* ===[ Functions ]========================================================== */

bool username_is_valid(const char *username, uint8_t length);

/**
 * Translates a magic constant into its name, or its hexadecimal representation
 * if not recognized.
 */
const char *magic_name(uint8_t);

/**
 * Translates a client_state into its name.
 */
const char *state_name(enum client_state);

#define fl() fflush(stdout)
#define check_alloc(ptr)\
	if ( ptr == NULL ) {\
		log_error("Errore su malloc()");\
		exit(EXIT_FAILURE);\
	}

#define XSTR(S) #S
#define STR(S) XSTR(S)
#define prompt(c) { fputs(STR(c)" ", stdout); fflush(stdout); }
/* E.g.: prompt(>) prompt($) */

/* ========================================================================== */

#endif
