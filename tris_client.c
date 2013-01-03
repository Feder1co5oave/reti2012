#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "pack.h"
#include "common.h"

#define update_maxfds(n) maxfds = (maxfds < (n) ? (n) : maxfds)
#define monitor_socket_r(sock) { FD_SET(sock, &readfds); update_maxfds(sock); }
#define monitor_socket_w(sock) { FD_SET(sock, &writefds); update_maxfds(sock); }
#define unmonitor_socket_r(sock) FD_CLR(sock, &readfds)
#define unmonitor_socket_w(sock) FD_CLR(sock, &writefds)
#define getl() fgets(buffer, BUFFER_SIZE, stdin)

void client_shell(void);
void got_play_request(void);
void server_disconnected(void);

char buffer[BUFFER_SIZE];

fd_set readfds, writefds;
int maxfds = -1;
struct timeval tv = DEFAULT_TIMEOUT_INIT;

int received, sent;
enum client_state state = NONE;
int sock_server, sock_opp, error;
uint16_t udp_port;

int main (int argc, char **argv) {
	struct sockaddr_in server_host;
	fd_set _readfds, _writefds;
	int sel_status;
	
	if (argc != 3) {
		printf("Usage: %s <server_ip> <server_port>\n", argv[0]);
		exit(EXIT_FAILURE);
	}
	
	if ( inet_pton(AF_INET, argv[1], &(server_host.sin_addr)) != 1 ) {
		printf("Indirizzo server non valido");
		exit(EXIT_FAILURE);
	}
	
	server_host.sin_family = AF_INET;
	server_host.sin_port = htons((uint16_t) atoi(argv[2])); /*FIXME check cast */
	memset(server_host.sin_zero, 0, sizeof(server_host.sin_zero));

	sock_server = socket(server_host.sin_family, SOCK_STREAM , 0);
	if ( sock_server == -1 ) {
		perror("Errore socket()");
		exit(EXIT_FAILURE);
	}
	
	if ( connect(sock_server, (struct sockaddr*) &server_host, sizeof(server_host)) != 0 ) {
		perror("Errore connect()");
		exit(EXIT_FAILURE);
	}
	
	state = CONNECTED;
	printf("Connessione al server avvenuta\n> "); fl();
	
	FD_ZERO(&readfds);
	FD_ZERO(&writefds);
	monitor_socket_r(STDIN_FILENO);
	monitor_socket_r(sock_server);
	_readfds = readfds;
	_writefds = writefds;
	
	
	while ( (sel_status = select(maxfds + 1, &_readfds, &_writefds, NULL, &tv)) > 0 ) {
		uint8_t cmd, resp;
		
		if ( FD_ISSET(STDIN_FILENO, &_readfds) ) {
			client_shell();
		} else if ( state != NONE && FD_ISSET(sock_server, &_readfds) ) {
			received = recv(sock_server, &cmd, 1, 0);
			if ( received != 1 ) server_disconnected();
			
			switch ( cmd ) {
				case REQ_PLAY:
					if ( state == FREE ) {
						got_play_request();
					} else {
						resp = RESP_BUSY;
						send(sock_server, &resp, 1, 0); /*FIXME è bloccante */
					}
					break;
				
				case RESP_OK_PLAY:
					/*TODO */
					break;
				
				default:
					printf("Response dal server non richiesta: 0x%hx\n> ", (uint16_t) cmd); fl();
					/*FIXME */
			}
		} else if ( state == PLAY && FD_ISSET(sock_opp, &_readfds) ) {
			
		} else if ( state != NONE && FD_ISSET(sock_server, &_writefds) ) {
			
		} else if ( state == PLAY && FD_ISSET(sock_opp, &_writefds) ) {
			
		} else {
			puts("I'm a bug.");
		}
		_readfds = readfds;
		_writefds = writefds;
	}

	puts("Errore su select()");
	shutdown(sock_server, SHUT_RDWR); /*TODO get rid of all the shutdown() calls */
	close(sock_server);
	exit(EXIT_FAILURE);
}

void client_shell() {
	int line_length, cmd_length, s;
	uint32_t size;
	char *cmd;
	getl(); /*TODO check return value */
	line_length = strlen(buffer) - 1;
	
	if ( line_length <= 0 ) {
		printf("> "); fl();
		return;
	}
	
	cmd = buffer + line_length + 2;
	sscanf(buffer, "%s", cmd);
	cmd_length = strlen(cmd);
	
	if ( strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0 ) {
		printf("Commands: help, login, who, play, exit\n> "); fl();
	} else if ( strcmp(cmd, "login") == 0 ) {
		char user[50]; int user_length;
		uint8_t resp;
		uint16_t port;

		if ( state != CONNECTED ) {
			printf("Sei già loggato\n> ");
			fl();
			return;
		}

		s = sscanf(buffer + cmd_length, "%s %hu", user, &port); /*FIXME controllo sul parsing di %hu */
		if ( s != 2 ) {
			printf("Sintassi: login <username> <porta udp>\n> "); fl();
			return;
		}

		/*TODO check se user corrisponde all'utente attuale */
		user_length = strlen(user);
		pack(buffer, "bbsw", REQ_LOGIN, user_length, user, port);
		sent = send(sock_server, buffer, 4 + user_length, 0);
		if ( sent != 4 + user_length ) server_disconnected();
		received = recv(sock_server, &resp, 1, 0);
		if ( received != 1 ) server_disconnected();
		
		switch ( resp ) {
			case RESP_OK_LOGIN:
				state = FREE;
				printf("Loggato con successo\n> "); fl();
				break;
			case RESP_EXIST:
				printf("Lo username esiste già\n> "); fl();
				break;
			case RESP_BADUSR:
				printf("Lo username ha un formato non valido\n> "); fl();
				break;
			case RESP_BADREQ:
				printf("BADREQ\n> "); fl();
				break;
			default:
				printf("Response dal server non richiesta: 0x%hx\n> ", (uint16_t) buffer[0]); fl();
		}
	} else if ( strcmp(cmd, "who") == 0 ) {
		uint8_t resp, length;
		uint32_t i;

		if ( state == CONNECTED ) {
			printf("Devi prima loggarti\n> ");
			fl();
			return;
		}

		buffer[0] = REQ_WHO;
		sent = send(sock_server, buffer, 1, 0);
		if ( sent != 1 ) server_disconnected();
		received = recv(sock_server, &resp, 1, 0);
		if ( received != 1 ) server_disconnected();
		switch ( resp ) {
			case RESP_WHO:
				recv(sock_server, buffer, 4, 0);
				unpack(buffer, "l", &size);
				printf("Ci sono %d client connessi\n", size);
				if (size > 100) exit(EXIT_FAILURE); /*FIXME debug statement */
				for (i = 0; i < size; i++) {
					recv(sock_server, &length, 1, 0);
					recv(sock_server, buffer, length, 0);
					buffer[length] = '\0';
					printf("[%s]\n", buffer);
				}
				printf("> "); fl();
				break;
			
			case RESP_BADREQ:
				printf("BADREQ\n> "); fl();
				break;
			
			default:
				printf("Response dal server non richiesta: 0x%hx\n> ", (uint16_t) buffer[0]); fl();
		}
		
	} else if ( strcmp(cmd, "play") == 0 ) {
		char user[50]; int user_length;
		uint8_t resp;
		struct in_addr opp_ip;
		uint16_t opp_port;

		if (state != FREE) {
			printf("Devi essere loggato e libero per iniziare una nuova partita\n> ");
			fl();
			return;
		}
		
		s = sscanf(buffer + cmd_length, "%s", user);
		if (s != 1) {
			printf("Specifica il nome del giocatore con cui vuoi giocare\n> ");
			fl();
			return;
		}

		/*TODO controllo se user != my_username */

		user_length = strlen(user);
		pack(buffer, "bbs", REQ_PLAY, (uint8_t) user_length, user);
		sent = send(sock_server, buffer, 2 + user_length, 0);
		if ( sent != 2 + user_length ) server_disconnected();
		printf("Richiesta di gioco con [%s] inviata, in attesa di risposta...\n", user);
		state = BUSY;
		received = recv(sock_server, &resp, 1, 0);
		if ( received != 1 ) server_disconnected();
		
		switch ( resp ) {
			case RESP_OK_PLAY:
				received = recv(sock_server, buffer, 6, 0);
				if ( received != 6 ) server_disconnected();
				unpack(buffer, "lw", &opp_ip, &opp_port);
				inet_ntop(AF_INET, &opp_ip, buffer, INET_ADDRSTRLEN);
				printf("[%s] ha accettato la richiesta di gioco.\nMi connetto all'host %s:%hu...",
					user, buffer, opp_port);
				printf("\nStai giocando con [%s]\n> ", user);
				state = PLAY;
				/*TODO */
				break;
			
			case RESP_REFUSE:
				printf("[%s] ha rifiutato la richiesta di gioco.\n> ", user); fl();
				state = FREE;
				break;
			
			case RESP_NONEXIST:
				printf("[%s] non esiste.\n> ", user); fl();
				state = FREE;
				break;
			
			case RESP_BUSY:
				printf("[%s] è già occupato in un altra partita.\n> ", user); fl();
				state = FREE;
				break;
				
			case RESP_BADREQ:
				printf("BADREQ\n> "); fl();
				break;
			
			default:
				printf("Response dal server non richiesta: 0x%hx\n> ", (uint16_t) buffer[0]); fl();
		}
	} else if ( strcmp(cmd, "end") == 0 ) {
		buffer[0] = (char) REQ_END;
		sent = send(sock_server, buffer, 1, 0);
		if ( sent != 1 ) server_disconnected();
		received = recv(sock_server, buffer, 1, 0);
		if ( received != 1 ) server_disconnected();
		if ( buffer[0] == RESP_OK_FREE ) {
			state = FREE;
			printf("Hai terminato la partita e sei di nuovo libero\n> ");
			fl();
		} else {
			printf("Response dal server non richiesta: 0x%hx\n> ", (uint16_t) buffer[0]); fl();
		}
	} else if ( strcmp(cmd, "exit") == 0 ) {
		if ( state == PLAY ) {
			/*TODO */
		}
		shutdown(sock_server, SHUT_RDWR);
		close(sock_server);
		exit(EXIT_SUCCESS);
	} else if ( strcmp(buffer, "\n") == 0 ) {
		printf("> "); fl();
	} else {
		printf("Unknown command\n> "); fl();
	}
}

void got_play_request() {
	uint8_t length, resp;
	bool repeat = TRUE;
	state = BUSY;
	received = recv(sock_server, &length, 1, 0);
	if ( received != 1 ) server_disconnected();
	received = recv(sock_server, buffer, length, 0);
	if ( received != length ) server_disconnected();
	buffer[length] = '\0';
	printf("\nRichiesta di gioco da parte di [%s]. Accetta (y) o rifiuta (n) ?"
		"\n> ", buffer);
	fl();
	do {
		getl();
		if ( strcmp(buffer, "y\n") == 0 ) {
			repeat = FALSE;
			resp = RESP_OK_PLAY;
			sent = send(sock_server, &resp, 1, 0);
			if ( sent != 1 ) server_disconnected();
			state = PLAY;
			/*TODO */
			printf("Richiesta accettata.\nIn attesa della connessione dall'"
				"altro client...\n");
		} else if ( strcmp(buffer, "n\n") == 0 ) {
			repeat = FALSE;
			resp = RESP_REFUSE;
			sent = send(sock_server, &resp, 1, 0);
			if ( sent != 1 ) server_disconnected();
			state = FREE;
			printf("Richiesta rifiutata.\n> "); fl();
		} else {
			printf("Accetta (y) o rifiuta (n) ?\n> "); fl();
		}
	} while (repeat);
}

void server_disconnected() {
	printf("\nConnessione al server persa.\n");
	shutdown(sock_server, SHUT_RDWR);
	close(sock_server);
	exit(EXIT_FAILURE);
}
