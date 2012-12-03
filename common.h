#ifndef _COMMON_H
#define _COMMON_H

#include <sys/types.h>

#define MAX_USERNAME_LENGTH 30
#define DEFAULT_TIMEOUT_INIT {60, 0}


/* === Magic constants ====================================================== */

#define REQ_WHO 		0x62
#define REQ_END			0x99
#define REQ_PLAY 		0x26
#define RESP_WHO		0x36
#define RESP_NONEXIST 	0x83
#define RESP_REFUSE 	0x47
#define RESP_OK_PLAY 	0x16
#define RESP_BADREQ		0x54

/* ========================================================================== */

typedef uint8_t bool;
#define TRUE 1
#define FALSE 0

bool is_valid_username(const char *username);

#define check_alloc(ptr) if ( ptr == NULL ) { perror("Errore su malloc()"); exit(1); }

#endif
