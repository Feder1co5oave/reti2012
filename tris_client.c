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
#define log_statechange() flog_message(LOG_DEBUG, "I am now %s", state_name(my_state))



/* ===[ Helpers ]============================================================ */

void client_shell(void);
void got_play_request(void);
void get_play_response(void);
void list_connected_clients(void);
void login(void);
void send_play_request(int sock_server, const char *opp_username);
void server_disconnected(void);



/* ===[ Data ]=============================================================== */

char buffer[BUFFER_SIZE];

fd_set readfds, writefds;
int maxfds = -1;
struct log_file *console;

struct sockaddr_in my_host;
char               my_username[MAX_USERNAME_LENGTH + 1];
uint8_t            my_username_length;
enum client_state  my_state = NONE;
uint16_t           my_udp_port;

struct sockaddr_in opp_host;
int                opp_socket;
char               opp_username[MAX_USERNAME_LENGTH + 1];
uint8_t            opp_username_length;

int sock_server, error;



/* ===[ Main ]=============================================================== */

int main (int argc, char **argv) {
	struct timeval tv = DEFAULT_TIMEOUT_INIT;
	struct sockaddr_in server_host;
	socklen_t sockaddrlen = sizeof(struct sockaddr_in);
	int received;
	fd_set _readfds, _writefds;
	int sel_status;
	uint8_t resp;

	/* Set log files */
	console = new_log(stdout, LOG_CONSOLE | LOG_INFO | LOG_ERROR, FALSE);
	sprintf(buffer, "tris_client-%d.log", getpid());
	open_log(buffer, LOG_ALL);
	
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
	
	if ( connect(sock_server, (struct sockaddr*) &server_host,
                                                   sizeof(server_host)) != 0 ) {
		log_error("Error connect()");
		exit(EXIT_FAILURE);
	}
	
	my_state = CONNECTED;
	log_message(LOG_CONSOLE, "Connected to the server");
	log_statechange();
	
	login();
	
	console->prompt = '>';
	log_prompt(console);
	
	FD_ZERO(&readfds);
	FD_ZERO(&writefds);
	monitor_socket_r(STDIN_FILENO);
	monitor_socket_r(sock_server);
	_readfds = readfds;
	_writefds = writefds;
	
	
	while ( (sel_status = select(maxfds + 1, &_readfds, &_writefds, NULL, &tv)) >= 0 ) {
		uint8_t cmd;
		
		if ( sel_status == 0 && my_state == PLAY ) {
			/* TODO end_match(); */
			continue;
		}
		
		if ( FD_ISSET(STDIN_FILENO, &_readfds) ) {
			client_shell();
		} else if ( my_state != NONE && FD_ISSET(sock_server, &_readfds) ) {
			received = recv(sock_server, &cmd, 1, 0);
			if ( received != 1 ) server_disconnected();
			
			switch ( cmd ) {
				case REQ_PLAY:
					if ( my_state == FREE ) {
						got_play_request();
					} else {
						resp = RESP_BUSY;
						send(sock_server, &resp, 1, 0); /*FIXME è bloccante */
					}
					break;
				
				case RESP_OK_PLAY:
					/*TODO ? */
				
				default:
					flog_message(LOG_WARNING, "Unexpected server response: %s",
                                                               magic_name(cmd));
					/*FIXME */
			}
		} else if ( my_state == PLAY && FD_ISSET(opp_socket, &_readfds) ) {
			/*TODO */
		} else {
			/*FIXME */
			flog_message(LOG_WARNING, "Unexpected event on line %d", __LINE__);
		}
		_readfds = readfds;
		_writefds = writefds;
	}

	log_error("Error select()");
	shutdown(sock_server, SHUT_RDWR);
	/*TODO get rid of all the shutdown() calls */
	close(sock_server);
	exit(EXIT_FAILURE);
}

/* ========================================================================== */

void client_shell() {
	int line_length, cmd_length;
	char cmd[BUFFER_SIZE];
	int sent, received;
	
	fgets(buffer, BUFFER_SIZE, stdin);
	
	line_length = strlen(buffer) - 1;
	buffer[line_length] = '\0';
	sscanf(buffer, "%s", cmd); /*FIXME check return value */
	cmd_length = strlen(cmd);
	
	log_message(LOG_USERINPUT, buffer);
	
	if ( strcmp(buffer, "help") == 0 || strcmp(buffer, "?") == 0 ) {
		
		log_message(LOG_CONSOLE, "Commands: help, who, play, exit");
		
	} else if ( strcmp(buffer, "who") == 0 ) {
		
		/*FIXME if condition */
		/*FIXME if my_state == BUSY ? */
		if ( my_state == CONNECTED ) log_message(LOG_CONSOLE, "You are not logged in");
		else list_connected_clients();
		
	} else if ( strcmp(cmd, "play") == 0 ) {
		
		char user[50];
		
		sscanf(buffer + cmd_length, " %s", user);
		/*FIXME check return value == 1 */
		
		if (my_state != FREE) {
			log_message(LOG_CONSOLE, "You are not FREE");
			return;
		}
		
		if ( strlen(user) <= 0 ) {
			log_message(LOG_CONSOLE, "Write the name of the player you want to play with");
			return;
		}
		

		if ( strcmp(user, my_username) == 0 ) {
			log_message(LOG_CONSOLE, "You cannot play with yourself");
		} else {
			strcpy(opp_username, user);
			send_play_request(sock_server, opp_username);
			get_play_response();
		}
		
	} else if ( strcmp(cmd, "end") == 0 ) {
		buffer[0] = (char) REQ_END;
		sent = send(sock_server, buffer, 1, 0);
		if ( sent != 1 ) server_disconnected();
		received = recv(sock_server, buffer, 1, 0);
		if ( received != 1 ) server_disconnected();
		if ( buffer[0] == RESP_OK_FREE ) {
			my_state = FREE;
			log_message(LOG_CONSOLE, "End of match. You are now free.");
		} else {
			flog_message(LOG_WARNING, "Unexpected server response: %s", magic_name(buffer[0]));
		}
		/*TODO send REQ_END to opponent */
	} else if ( strcmp(buffer, "exit") == 0 ) {
		if ( my_state == PLAY ) {
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

void login() {
	int line_length, received, sent;
	int p;
	char *s;
	uint8_t resp;
	
	do {
		log_message(LOG_CONSOLE, "Insert your username:");
		s = fgets(buffer, BUFFER_SIZE, stdin); /*FIXME if ( s == NULL ) */
		line_length = strlen(buffer) - 1;
		if ( line_length >= 0 ) buffer[line_length] = '\0';
		
		log_message(LOG_USERINPUT, buffer);
		
		if ( line_length > MAX_USERNAME_LENGTH ) {
			log_message(LOG_CONSOLE, "This username is too long");
			continue;
		}
		
		if ( line_length < MIN_USERNAME_LENGTH ) {
			log_message(LOG_CONSOLE, "This username is too short");
			continue;
		}
		
		strcpy(my_username, buffer);
		my_username_length = line_length;
		
		log_message(LOG_CONSOLE, "Insert your UDP port:");
		s = fgets(buffer, BUFFER_SIZE, stdin); /*FIXME if ( s == NULL ) */
		line_length = strlen(buffer);
		if ( line_length >= 0 ) buffer[line_length - 1] = '\0';
		log_message(LOG_USERINPUT, buffer);
		p = sscanf(buffer, "%hu", &my_udp_port); /*FIXME if ( p != 1 ) */
		
		if ( my_udp_port < 1024 )
			log_message(LOG_CONSOLE,
                     "Your UDP port is in the system range, it might not work");
		
		pack(buffer, "bbsw", REQ_LOGIN, (uint8_t) my_username_length,
                                                      my_username, my_udp_port);
		
		my_host.sin_port = htons(my_udp_port);
		sent = send(sock_server, buffer, 4 + my_username_length, 0);
		if ( sent != 4 + my_username_length ) server_disconnected();
		received = recv(sock_server, &resp, 1, 0);
		if ( received != 1 ) server_disconnected();
		
		switch ( resp ) {
			case RESP_OK_LOGIN:
				my_state = FREE;
				flog_message(LOG_CONSOLE, "Successfully logged in as [%s]",
                                                                   my_username);
				break;
			case RESP_EXIST:
				log_message(LOG_CONSOLE, "This username is taken");
				break;
			case RESP_BADUSR:
				log_message(LOG_CONSOLE, "This username is badly formatted");
				break;
			default:
				flog_message(LOG_WARNING, "Unexpected server response: %s",
                                                              magic_name(resp));
		}
	} while ( resp != RESP_OK_LOGIN );
}

void got_play_request() {
	uint8_t length, resp;
	int line_length;
	bool repeat = TRUE;
	int received, sent;
	
	my_state = BUSY;
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
			my_state = PLAY;
			/*TODO */
			log_message(LOG_CONSOLE, "Request accepted. Waiting for connection "
                                                    "from the other client...");
			
		} else if ( strcmp(buffer, "n") == 0 ) {
			repeat = FALSE;
			resp = RESP_REFUSE;
			sent = send(sock_server, &resp, 1, 0);
			if ( sent != 1 ) server_disconnected();
			my_state = FREE;
			log_message(LOG_CONSOLE, "Request refused");
		} else {
			log_message(LOG_CONSOLE, "Accept (y) or refuse (n) ?");
		}
	} while (repeat);
}

void get_play_response() {
	uint8_t resp;
	uint16_t opp_udp_port;
	int received;
	
	received = recv(sock_server, &resp, 1, 0);
	if ( received != 1 ) server_disconnected();
	
	switch ( resp ) {
		case RESP_OK_PLAY:
			received = recv(sock_server, buffer, sizeof(struct in_addr) + 2, 0);
			if ( received != sizeof(struct in_addr) + 2 ) server_disconnected();
			
			opp_host.sin_family = AF_INET;
			memset(opp_host.sin_zero, 0, sizeof(opp_host.sin_zero));
			unpack(buffer, "lw", &(opp_host.sin_addr), &opp_udp_port);
			inet_ntop(AF_INET, &(opp_host.sin_addr), buffer, INET_ADDRSTRLEN);
			opp_host.sin_port = htons(opp_udp_port);
			
			flog_message(LOG_CONSOLE,
                    "[%s] accepted to play with you. Contacting host %s:%hu...",
                                            opp_username, buffer, opp_udp_port);
			
			my_state = PLAY;
			
			break;
		
		case RESP_REFUSE:
			flog_message(LOG_CONSOLE, "[%s] refused to play", opp_username);
			my_state = FREE;
			break;
		
		case RESP_NONEXIST:
			flog_message(LOG_CONSOLE, "[%s] does not exist", opp_username);
			my_state = FREE;
			break;
		
		case RESP_BUSY:
			flog_message(LOG_CONSOLE, "[%s] is occupied in another match", opp_username);
			my_state = FREE;
			break;
		
		default:
			flog_message(LOG_WARNING, "Unexpected server response: %s", magic_name(resp));
	}
}

void list_connected_clients() {
	int received, sent;
	uint8_t resp, length;
	uint32_t count, i;

	buffer[0] = REQ_WHO;
	
	sent = send(sock_server, buffer, 1, 0);
	if ( sent != 1 ) server_disconnected();
	received = recv(sock_server, &resp, 1, 0);
	if ( received != 1 ) server_disconnected();
	
	switch ( resp ) {
		case RESP_WHO:
			received = recv(sock_server, buffer, 4, 0);
			if ( received != 4 ) server_disconnected();
			unpack(buffer, "l", &count);
			if (count > 100) exit(EXIT_FAILURE); /*FIXME debug statement */

			console->prompt = FALSE;
			flog_message(LOG_CONSOLE, "There are %u connected clients", count);
			for (i = 0; i < count; i++) {
				received = recv(sock_server, &length, 1, 0);
				if ( received != 1 ) server_disconnected();
				received = recv(sock_server, buffer, length, 0);
				if ( received != length ) server_disconnected();
				buffer[length] = '\0';
				flog_message(LOG_CONSOLE, "[%s]", buffer);
			}
			console->prompt = '>';
			log_prompt(console);
			break;
		
		default:
			flog_message(LOG_WARNING, "Unexpected server response: %s", magic_name(resp));
	}
}

void send_play_request(int sock_server, const char *opp_username) {
	int sent;
	uint8_t username_length;
	
	username_length = strlen(opp_username);
	pack(buffer, "bbs", REQ_PLAY, username_length, opp_username);
	sent = send(sock_server, buffer, 2 + username_length, 0);
	if ( sent != 2 + username_length ) server_disconnected();
	flog_message(LOG_CONSOLE,
            "Sent play request to [%s], waiting for response...", opp_username);
	
	my_state = BUSY;
}

void server_disconnected() {
	log_message(LOG_ERROR, "Lost connection to server");
	shutdown(sock_server, SHUT_RDWR);
	close(sock_server);
	/*TODO if ( state == PLAYING ) */
	exit(EXIT_FAILURE);
}
