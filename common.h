#ifndef _COMMON_H
#define _COMMON_H

#include <sys/types.h>
#include <stdint.h>
#include <libintl.h>
#include <locale.h>
#include <stdarg.h>



/* ===[ Constants ]========================================================== */

#define MIN_USERNAME_LENGTH 3
#define MAX_USERNAME_LENGTH 30
#define USERNAME_ALPHABET \
             "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-"

#define DEFAULT_TIMEOUT_INIT {60, 0}      /* 60 seconds */
#define HELLO_TIMEOUT (10 * 1000)         /* 10 seconds */
#define PLAY_RESPONSE_TIMEOUT (60 * 1000) /* 60 seconds */

#define BUFFER_SIZE 1024



/* ===[ Magic constants ]==================================================== */

#define REQ_LOGIN       0x07
#define REQ_WHO         0x62
#define REQ_END         0x99
#define REQ_PLAY        0x26

#define RESP_OK_LOGIN   0x94
#define RESP_EXIST      0x49
#define RESP_BADUSR     0x24

#define RESP_WHO        0x36

#define RESP_BUSY       0x41
#define RESP_NONEXIST   0x83
#define RESP_REFUSE     0x47
#define RESP_OK_PLAY    0x16

#define REQ_HELLO       0x44
#define REQ_HIT         0x66

#define RESP_OK_FREE    0x72

#define RESP_BADREQ     0x54



/* ===[ Data types ]========================================================= */

/**
 * Describes a client state by the viewpoint of the server.
 */
enum client_state {
	NONE,       /* default value, client does not exist yet                   */
	CONNECTED,  /* client is connected, not logged in yet                     */
	FREE,       /* client is connected and logged in (username and udp port)  */
	BUSY,       /* client has/is requested to play a match                    */
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

/**
 * Encodes a client_state into a byte to be sent over the network.
 */
uint8_t state_encode(enum client_state);

/**
 * Decodes an encoded client_state.
 */
enum client_state state_decode(uint8_t);

#define _(STR) gettext(STR)
#define check_alloc(ptr)\
	if ( ptr == NULL ) {\
		log_error(_("Error malloc()"));\
		exit(EXIT_FAILURE);\
	}

/**
 * Get a line from stdin. Must compile in C89 (-ansi).
 * @param char *buffer the buffer used to store the string
 * @param int size the size of the buffer
 * @return int the length of the line
 */
int get_line(char *buffer, int size);

/**
 * Send a buffer through socket, iterating until all data is sent.
 * @param int socket
 * @param const char *buffer
 * @param int length the number of bytes to be sent
 * @return int 0 on success, < 0 on send() error
 */
int send_buffer(int socket, const char *buffer, int length);

/**
 * Send a byte through socket.
 * @param int socket
 * @param uint8_t byte
 * @return int @see send_buffer()
 */
int send_byte(int socket, uint8_t byte);

/**
 * Format specifiers:
 * b - 8 bit
 * w - 16 bit
 * l - 32 bit
 * s - 0-ended string, strip the \0
 * S - 0-ended string, keep the \0
 * All numbers are to be provided in host-byte-order, they will be converted to
 * network-byte-order.
 */
int pack(void *buffer, const char *format, ...);

/**
 * Format specifiers:
 * b
 * w
 * l
 * s - fixed length string. The length must be provided as argument, before the
 * char* (e.g. unpack(buff, "s", 12, &str) extracts 12 chars).
 * All numbers are extracted from network-byte-order to host-byte-order.
 * Strings will be 0-ended.
 */
void unpack(const void *buffer, const char *format, ...);

/* ========================================================================== */

#endif
