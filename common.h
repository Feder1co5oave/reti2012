#ifndef _COMMON_H
#define _COMMON_H

#include <sys/types.h>



/* ===[ Constants ]========================================================== */

#define MAX_USERNAME_LENGTH 30
#define USERNAME_ALPHABET "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.,-+*"
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

enum client_state { NONE, CONNECTED, FREE, BUSY, PLAY };

typedef unsigned char bool;
#define TRUE 1
#define FALSE 0



/* ===[ Functions ]========================================================== */

bool username_is_valid(const char *username);

#define check_alloc(ptr) if ( ptr == NULL ) { perror("Errore su malloc()"); exit(1); }
#define fl() fflush(stdout)

/* ========================================================================== */

#endif
