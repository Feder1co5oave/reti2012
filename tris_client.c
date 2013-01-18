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
#include "log.h"

#define update_maxfds(n) maxfds = (maxfds < (n) ? (n) : maxfds)
#define monitor_socket_r(sock) { FD_SET(sock, &readfds); update_maxfds(sock); }
#define monitor_socket_w(sock) { FD_SET(sock, &writefds); update_maxfds(sock); }
#define unmonitor_socket_r(sock) FD_CLR(sock, &readfds)
#define unmonitor_socket_w(sock) FD_CLR(sock, &writefds)

void client_shell(void);
void got_play_request(void);
void server_disconnected(void);

char buffer[BUFFER_SIZE];

fd_set readfds, writefds;
int maxfds = -1;
struct timeval tv = DEFAULT_TIMEOUT_INIT;
struct log_file *console;

int received, sent;
enum client_state state = NONE;
int sock_server, sock_opp, error;
uint16_t udp_port;

int main (int argc, char **argv) {
	struct sockaddr_in server_host;
	fd_set _readfds, _writefds;
	int sel_status;

	/* Set log files */
	console = new_log(stdout, LOG_CONSOLE | LOG_INFO | LOG_ERROR, FALSE);
	open_log("tris_client.log", LOG_ALL);
	
	if (argc != 3) {
		flog_message(LOG_CONSOLE, "Usage: %s <server_ip> <server_port>", argv[0]);
		exit(EXIT_FAILURE);
	}
	
	if ( inet_pton(AF_INET, argv[1], &(server_host.sin_addr)) != 1 ) {
		log_message(LOG_CONSOLE, "Invalid server address");
		exit(EXIT_FAILURE);
	}
	
	server_host.sin_family = AF_INET;
	server_host.sin_port = htons((uint16_t) atoi(argv[2])); /*FIXME check cast */
	memset(server_host.sin_zero, 0, sizeof(server_host.sin_zero));

	sock_server = socket(server_host.sin_family, SOCK_STREAM , 0);
	if ( sock_server == -1 ) {
		log_error("Error socket()");
		exit(EXIT_FAILURE);
	}
	
	if ( connect(sock_server, (struct sockaddr*) &server_host, sizeof(server_host)) != 0 ) {
		log_error("Error connect()");
		exit(EXIT_FAILURE);
	}
	
	state = CONNECTED;
	log_message(LOG_CONSOLE, "Connected to the server");
	console->prompt = '>';
	log_prompt(console);
	
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
					flog_message(LOG_WARNING, "Unexpected server response: %s", magic_name(cmd));
					/*FIXME */
			}
		} else if ( state == PLAY && FD_ISSET(sock_opp, &_readfds) ) {
			
		} else if ( state != NONE && FD_ISSET(sock_server, &_writefds) ) {
			
		} else if ( state == PLAY && FD_ISSET(sock_opp, &_writefds) ) {
			
		} else {
			flog_message(LOG_WARNING, "Unexpected event on line %d", __LINE__);
		}
		_readfds = readfds;
		_writefds = writefds;
	}

	log_error("Error select()");
	shutdown(sock_server, SHUT_RDWR); /*TODO get rid of all the shutdown() calls */
	close(sock_server);
	exit(EXIT_FAILURE);
}

void client_shell() {
	int line_length, cmd_length, s;
	uint32_t size;
	char *cmd;
	
	fgets(buffer, BUFFER_SIZE, stdin);
	
	line_length = strlen(buffer);
	buffer[line_length - 1] = '\0';
	
	log_message(LOG_USERINPUT, buffer);

	cmd = buffer + line_length + 2;
	s = sscanf(buffer, "%s", cmd); /*FIXME check s */
	cmd_length = strlen(cmd);
	
	if ( strcmp(buffer, "help") == 0 || strcmp(buffer, "?") == 0 ) {
		log_message(LOG_CONSOLE, "Commands: help, login, who, play, exit");
	} else if ( strcmp(cmd, "login") == 0 ) {
		char user[50]; int user_length;
		uint8_t resp;
		uint16_t port;

		if ( state != CONNECTED ) {
			log_message(LOG_CONSOLE, "You are already logged in");
			return;
		}

		s = sscanf(buffer + cmd_length, "%s %hu", user, &port); /*FIXME controllo sul parsing di %hu */
		if ( s != 2 ) {
			log_message(LOG_CONSOLE, "Syntax: login <username> <udp_port>");
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
				log_message(LOG_CONSOLE, "Successfully logged in");
				break;
			case RESP_EXIST:
				log_message(LOG_CONSOLE, "This username is taken");
				break;
			case RESP_BADUSR:
				log_message(LOG_CONSOLE, "This username is badly formatted");
				break;
			case RESP_BADREQ:
				log_message(LOG_WARNING, "BADREQ");
				break;
			default:
				flog_message(LOG_WARNING, "Unexpected server response: %s", magic_name(resp));
		}
	} else if ( strcmp(buffer, "who") == 0 ) {
		uint8_t resp, length;
		uint32_t i;

		if ( state == CONNECTED ) {
			log_message(LOG_CONSOLE, "You are not logged in");
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
				if (size > 100) exit(EXIT_FAILURE); /*FIXME debug statement */

				console->prompt = FALSE;
				flog_message(LOG_CONSOLE, "There are %u connected clients", size);
				for (i = 0; i < size; i++) {
					recv(sock_server, &length, 1, 0);
					recv(sock_server, buffer, length, 0);
					buffer[length] = '\0';
					flog_message(LOG_CONSOLE, "[%s]", buffer);
				}
				console->prompt = '>';
				log_prompt(console);
				break;
			
			case RESP_BADREQ:
				log_message(LOG_WARNING, "BADREQ");
				break;
			
			default:
				flog_message(LOG_WARNING, "Unexpected server response: %s", magic_name(resp));
		}
		
	} else if ( strcmp(cmd, "play") == 0 ) {
		char user[50]; int user_length;
		uint8_t resp;
		struct in_addr opp_ip;
		uint16_t opp_port;

		if (state != FREE) {
			log_message(LOG_CONSOLE, "You are not FREE");
			return;
		}
		
		s = sscanf(buffer + cmd_length, "%s", user);
		if (s != 1) {
			log_message(LOG_CONSOLE, "Write the name of the player you want to play with");
			return;
		}

		/*TODO controllo se user != my_username */

		user_length = strlen(user);
		pack(buffer, "bbs", REQ_PLAY, (uint8_t) user_length, user);
		sent = send(sock_server, buffer, 2 + user_length, 0);
		if ( sent != 2 + user_length ) server_disconnected();
		flog_message(LOG_CONSOLE, "Sent play request to [%s], waiting for response...", user);
		state = BUSY;
		received = recv(sock_server, &resp, 1, 0);
		if ( received != 1 ) server_disconnected();
		
		switch ( resp ) {
			case RESP_OK_PLAY:
				received = recv(sock_server, buffer, 6, 0);
				if ( received != 6 ) server_disconnected();
				unpack(buffer, "lw", &opp_ip, &opp_port);
				inet_ntop(AF_INET, &opp_ip, buffer, INET_ADDRSTRLEN);
				flog_message(LOG_CONSOLE, "[%s] accepted to play with you. Contacting host %s:%hu...", user, buffer, opp_port);
				flog_message(LOG_CONSOLE, "Playing with [%s]...", user);
				state = PLAY;
				/*TODO */
				break;
			
			case RESP_REFUSE:
				flog_message(LOG_CONSOLE, "[%s] refused to play", user);
				state = FREE;
				break;
			
			case RESP_NONEXIST:
				flog_message(LOG_CONSOLE, "[%s] does not exist", user);
				state = FREE;
				break;
			
			case RESP_BUSY:
				flog_message(LOG_CONSOLE, "[%s] is occupied in another match", user);
				state = FREE;
				break;
				
			case RESP_BADREQ:
				log_message(LOG_WARNING, "BADREQ");
				break;
			
			default:
				flog_message(LOG_WARNING, "Unexpected server response: %s", magic_name(resp));
		}
	} else if ( strcmp(cmd, "end") == 0 ) {
		buffer[0] = (char) REQ_END;
		sent = send(sock_server, buffer, 1, 0);
		if ( sent != 1 ) server_disconnected();
		received = recv(sock_server, buffer, 1, 0);
		if ( received != 1 ) server_disconnected();
		if ( buffer[0] == RESP_OK_FREE ) {
			state = FREE;
			log_message(LOG_CONSOLE, "End of match. You are now free.");
		} else {
			flog_message(LOG_WARNING, "Unexpected server response: %s", magic_name(buffer[0]));
		}
	} else if ( strcmp(buffer, "exit") == 0 ) {
		if ( state == PLAY ) {
			/*TODO */
		}
		shutdown(sock_server, SHUT_RDWR);
		close(sock_server);
		exit(EXIT_SUCCESS);
	} else if ( strcmp(buffer, "") == 0 ) {
		log_prompt(console);
	} else {
		log_message(LOG_CONSOLE, "Unknown command");
	}
}

void got_play_request() {
	uint8_t length, resp;
	int line_length;
	bool repeat = TRUE;
	
	state = BUSY;
	received = recv(sock_server, &length, 1, 0);
	if ( received != 1 ) server_disconnected();
	received = recv(sock_server, buffer, length, 0);
	if ( received != length ) server_disconnected();
	buffer[length] = '\0';
	flog_message(LOG_INFO, "Got play request from [%s]. Accept (y) or refuse (n) ?", buffer);
	do {
		fgets(buffer, BUFFER_SIZE, stdin);
		line_length = strlen(buffer);
		buffer[line_length - 1] = '\0';

		if ( strcmp(buffer, "y") == 0 ) {
			repeat = FALSE;
			resp = RESP_OK_PLAY;
			sent = send(sock_server, &resp, 1, 0);
			if ( sent != 1 ) server_disconnected();
			state = PLAY;
			/*TODO */
			log_message(LOG_CONSOLE, "Request accepted. Waiting for connection from the other client...");
		} else if ( strcmp(buffer, "n") == 0 ) {
			repeat = FALSE;
			resp = RESP_REFUSE;
			sent = send(sock_server, &resp, 1, 0);
			if ( sent != 1 ) server_disconnected();
			state = FREE;
			log_message(LOG_CONSOLE, "Request refused");
		} else {
			log_message(LOG_CONSOLE, "Accept (y) or refuse (n) ?");
		}
	} while (repeat);
}

void server_disconnected() {
	log_message(LOG_CONSOLE, "Lost connection to server");
	shutdown(sock_server, SHUT_RDWR);
	close(sock_server);
	exit(EXIT_FAILURE);
}
