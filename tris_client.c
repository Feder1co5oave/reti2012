#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/poll.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "common.h"
#include "log.h"
#include "tris_game.h"

#define PROMPT_FREE '>'
#define PROMPT_PLAY '#'

#define update_maxfds(n) maxfds = (maxfds < (n) ? (n) : maxfds)
#define monitor_socket_r(sock) { FD_SET(sock, &readfds); update_maxfds(sock); }
#define monitor_socket_w(sock) { FD_SET(sock, &writefds); update_maxfds(sock); }
#define unmonitor_socket_r(sock) FD_CLR(sock, &readfds)
#define unmonitor_socket_w(sock) FD_CLR(sock, &writefds)
#define log_statechange() \
                    flog_message(LOG_DEBUG, "I am now %s", state_name(my_state))

#define print_ip(host) \
        inet_ntop(AF_INET, &((host).sin_addr), buffer, sizeof(struct sockaddr_in))



/* ===[ Helpers ]============================================================ */

bool connect_play_socket(struct sockaddr_in *host);
void end_match(bool send_opp);
void free_shell(void);
bool get_hello(void);
void get_play_response(void);
void got_hit_or_end(void);
void got_play_request(void);
void list_connected_clients(void);
void login(void);
void make_move(unsigned int cell, bool send_opp);
bool open_play_socket(void);
void play_shell(void);
int poll_in(int socket, int timeout);
bool say_hello(void);
void send_play_request(void);
void server_disconnected(void);
void start_match(char me);



/* ===[ Data ]=============================================================== */

char buffer[BUFFER_SIZE];

fd_set readfds, writefds;
int maxfds = -1;
struct log_file *console;

struct sockaddr_in my_host;
char               my_username[MAX_USERNAME_LENGTH + 1];
enum client_state  my_state = NONE;

struct sockaddr_in opp_host;
int                opp_socket;
char               opp_username[MAX_USERNAME_LENGTH + 1];

int sock_server, error;

struct tris_grid grid = TRIS_GRID_INIT;
char my_player, turn;



/* ===[ Main ]=============================================================== */

int main (int argc, char **argv) {
	struct timeval tv = DEFAULT_TIMEOUT_INIT, _tv;
	struct sockaddr_in server_host;
	socklen_t sockaddrlen = sizeof(struct sockaddr_in);
	fd_set _readfds, _writefds;
	int sel_status;

	/* Set log files */
	console = new_log(stdout, LOG_CONSOLE | LOG_INFO | LOG_ERROR, FALSE);
	sprintf(buffer, "logs/tris_client-%d.log", getpid());
	open_log(buffer, LOG_ALL);
	
	if (argc != 3) {
		flog_message(LOG_CONSOLE, "Usage: %s <server_ip> <server_port>",
                                                                       argv[0]);
		
		exit(EXIT_FAILURE);
	}
	
	if ( inet_pton(AF_INET, argv[1], &(server_host.sin_addr)) != 1 ) {
		log_message(LOG_CONSOLE, "Invalid server address");
		exit(EXIT_FAILURE);
	}
	
	server_host.sin_family = AF_INET;
	server_host.sin_port = htons((uint16_t) atoi(argv[2]));
	/*FIXME check cast */
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
	
	if ( getsockname(sock_server, (struct sockaddr*) &my_host, &sockaddrlen) !=
                               0 || sockaddrlen > sizeof(struct sockaddr_in) ) {
		
		log_error("Error getsockname()");
		exit(EXIT_FAILURE);
	}
	
	print_ip(my_host);
	flog_message(LOG_DEBUG, "I am host %s:%hu", buffer, ntohs(my_host.sin_port));
	
	my_state = CONNECTED;
	log_message(LOG_CONSOLE, "Connected to the server");
	log_statechange();
	
	login();
	
	console->prompt = PROMPT_FREE;
	log_prompt(console);
	
	FD_ZERO(&readfds);
	FD_ZERO(&writefds);
	monitor_socket_r(STDIN_FILENO);
	monitor_socket_r(sock_server);
	_readfds = readfds;
	_writefds = writefds;
	_tv = tv;
	
	
	while ( (sel_status = select(maxfds + 1, &_readfds, &_writefds, NULL, &_tv))
                                                                        >= 0 ) {
		
		if ( sel_status == 0 ) {
			flog_message(LOG_DEBUG, "Select() timed out while %s",
                                                          state_name(my_state));
			
			if ( my_state == PLAY ) {
				log_message(LOG_INFO, "Time out: leaving game...");
				end_match(FALSE);
				log_prompt(console);
			} else {
				_readfds = readfds;
				_writefds = writefds;
				_tv = tv;
				continue;
			}
		}
		
		if ( FD_ISSET(STDIN_FILENO, &_readfds) ) {
			
			switch ( my_state ) {
				case FREE: free_shell(); break;
				case PLAY: play_shell(); break;
				default:
					get_line(buffer, BUFFER_SIZE);
					log_message(LOG_USERINPUT, buffer);
					flog_message(LOG_WARNING,
                     "Got unwanted user input while %s:", state_name(my_state));
					log_message(LOG_ERROR, ""); /*FIXME */
			}
			log_prompt(console);
			
		} else if ( FD_ISSET(sock_server, &_readfds) ) {
			
			uint8_t cmd;
			
			if ( recv(sock_server, &cmd, 1, 0) != 1 )
				server_disconnected();
			
			switch ( cmd ) {
				case REQ_PLAY:
					flog_message(LOG_DEBUG, "Got REQ_PLAY from server while %s",
                                                          state_name(my_state));
					
					if ( my_state == FREE ) {
						got_play_request();
						log_prompt(console);
					} else if ( send_byte(sock_server, RESP_REFUSE) < 0 )
						server_disconnected();
					break;
				
				case RESP_OK_PLAY:
					recv(sock_server, buffer, sizeof(struct in_addr) + 2, 0);
					/*FIXME check return value */
				case RESP_BUSY:
				case RESP_NONEXIST:
				case RESP_REFUSE:
					flog_message(LOG_INFO_VERBOSE,
                               "Got delayed play response=%s", magic_name(cmd));
					break;
				default:
					flog_message(LOG_WARNING, "Unexpected server cmd: %s",
                                                               magic_name(cmd));
				
					if ( cmd != RESP_BADREQ &&
                                       send_byte(sock_server, RESP_BADREQ) < 0 )
					
						server_disconnected();
					/*FIXME */
			}
			
		} else if ( my_state == PLAY && FD_ISSET(opp_socket, &_readfds) ) {
			
			got_hit_or_end();
			log_prompt(console);
			
		} else {
			/*FIXME */
			flog_message(LOG_WARNING, "Unexpected event on line %d while %s",
                                                __LINE__, state_name(my_state));
			
			sleep(1);
		}
		
		_readfds = readfds;
		_writefds = writefds;
		_tv = tv;
	}

	log_error("Error select()");
	shutdown(sock_server, SHUT_RDWR);
	/*TODO get rid of all the shutdown() calls */
	close(sock_server);
	exit(EXIT_FAILURE);
}

/* ========================================================================== */

void free_shell() {
	char cmd[BUFFER_SIZE] = "";
	
	get_line(buffer, BUFFER_SIZE);
	sscanf(buffer, "%s", cmd); /*FIXME check return value */
	log_message(LOG_USERINPUT, buffer);
	
	if ( strcmp(buffer, "help") == 0 ) { /* -------------------------- > help */
		
		log_message(LOG_CONSOLE, "Commands: exit, help, play, who");
		
	} else if ( strcmp(buffer, "who") == 0 ) { /* --------------------- > who */
		
		list_connected_clients();
		
	} else if ( strcmp(cmd, "play") == 0 ) { /* ---------------------- > play */
		
		char username[50];
		int s, username_length;
		
		s = sscanf(buffer, "play %s", username);
		username_length = strlen(username);
		
		if ( username_length <= 0 || s != 1 ) {
			log_message(LOG_CONSOLE, "Syntax: play <player>");
			return;
		}
		
		if ( username_length > MAX_USERNAME_LENGTH ) {
			log_message(LOG_CONSOLE, "This username is too long");
			return;
		}
		
		if ( username_length < MIN_USERNAME_LENGTH ) {
			log_message(LOG_CONSOLE, "This username is too short");
			return;
		}
		
		if ( !username_is_valid(username, username_length) ) {
			log_message(LOG_CONSOLE, "This username is badly formatted");
			return;
		}
		
		if ( strcmp(username, my_username) == 0 ) {
			log_message(LOG_CONSOLE, "You cannot play with yourself");
			return;
		}
		
		strcpy(opp_username, username);
		send_play_request();
		get_play_response();
		
	} else if ( strcmp(buffer, "exit") == 0 ) { /* ------------------- > exit */
		
		shutdown(sock_server, SHUT_RDWR);
		close(sock_server);
		exit(EXIT_SUCCESS);
		
	} else if ( strcmp(buffer, "") != 0 ) {
		
		log_message(LOG_CONSOLE,
                         "Unknown command. Type 'help' for a list of commands");
		
	}
}

bool open_play_socket() {
	opp_socket = socket(AF_INET, SOCK_DGRAM, 0);
	if ( opp_socket == -1 ) {
		log_error("Error socket(SOCK_DGRAM)");
		return FALSE;
	}
	
	log_message(LOG_DEBUG, "Opened opp_socket");
	
	if ( bind(opp_socket, (struct sockaddr*) &my_host, sizeof(my_host)) != 0 ) {
		
		log_error("Error bind(SOCK_DGRAM)");
		close(opp_socket);
		opp_socket = -1;
		return FALSE;
	}
	
	print_ip(my_host);
	flog_message(LOG_DEBUG, "Bound opp_socket to %s:%hu", buffer,
                                                       ntohs(my_host.sin_port));
	
	return TRUE;
}

bool connect_play_socket(struct sockaddr_in *host) {
	struct sockaddr_in null_host;
	
	if ( host == NULL ) {
		memset(&null_host, 0, sizeof(null_host));
		null_host.sin_family = AF_UNSPEC;
		host = &null_host;
	}
	
	if ( connect(opp_socket, (struct sockaddr*) host, sizeof(*host)) != 0 ) {
		log_error("Error connect(SOCK_DGRAM)");
		return FALSE;
	}
	
	print_ip(*host);
	flog_message(LOG_DEBUG, "Connected opp_socket to %s:%hu", buffer,
                                                         ntohs(host->sin_port));
	
	return TRUE;
}

void end_match(bool send_opp) {
	uint8_t resp;
	
	if ( send_opp && send_byte(opp_socket, REQ_END) < 0 )
		log_error("Error send()");
	
	if ( send_byte(sock_server, REQ_END) < 0 ) server_disconnected();
	if ( recv(sock_server, &resp, 1, 0) != 1 ) server_disconnected();
	
	my_state = FREE;
	log_statechange();
	
	if ( resp != RESP_OK_FREE )
		flog_message(LOG_WARNING, "Unexpected server response: %s",
                                                              magic_name(resp));
	
	log_message(LOG_CONSOLE, "End of match. You are now free.");
	
	if ( !connect_play_socket(&my_host) )
		log_message(LOG_WARNING, "Cannot de-connect opp_socket");
	
	if ( opp_socket > 0 ) unmonitor_socket_r(opp_socket);
	
	memset(&opp_host, 0, sizeof(opp_host));
	opp_username[0] = '\0';
	console->prompt = PROMPT_FREE;
}

void login() {
	int username_length;
	uint8_t resp;
	unsigned int my_udp_port;
	
	do {
		log_message(LOG_CONSOLE, "Insert your username:");
		username_length = get_line(buffer, BUFFER_SIZE);
		log_message(LOG_USERINPUT, buffer);
		
		if ( username_length > MAX_USERNAME_LENGTH ) {
			log_message(LOG_CONSOLE, "This username is too long");
			continue;
		}
		
		if ( username_length < MIN_USERNAME_LENGTH ) {
			log_message(LOG_CONSOLE, "This username is too short");
			continue;
		}
		
		if ( !username_is_valid(buffer, username_length) ) {
			log_message(LOG_CONSOLE, "This username is badly formatted");
			continue;
		}
		
		strcpy(my_username, buffer);
		
		do {
			log_message(LOG_CONSOLE, "Insert your UDP port:");
			get_line(buffer, BUFFER_SIZE);
			log_message(LOG_USERINPUT, buffer);
			if ( sscanf(buffer, " %u", &my_udp_port) != 1 ) continue;
			
			if ( my_udp_port == 0 || my_udp_port >= (1 << 16) ) {
				log_message(LOG_CONSOLE, "Your UDP port is not in port range");
				continue;
			}
			
			if ( my_udp_port < (1 << 10) )
				log_message(LOG_INFO, "Your UDP port is in the system range, "
                                                           "it might not work");
			
			my_host.sin_port = htons((uint16_t) my_udp_port);
			
			if ( !open_play_socket() ) {
				log_message(LOG_CONSOLE,
                         "This UDP port is already in use, choose another one");
				
				continue;
			}
			
			break;
		} while (TRUE);
		
		pack(buffer, "bbsw", REQ_LOGIN, (uint8_t) username_length, my_username,
                                                        (uint16_t) my_udp_port);
		
		
		if ( send_buffer(sock_server, buffer, 4 + username_length) < 0 )
			server_disconnected();
		
		if ( recv(sock_server, &resp, 1, 0) != 1 )
			server_disconnected();
		
		switch ( resp ) {
			case RESP_OK_LOGIN:
				my_state = FREE;
				flog_message(LOG_CONSOLE, "Successfully logged in as [%s]",
                                                                   my_username);
				
				flog_message(LOG_INFO_VERBOSE, "UDP port is %hu",
                                                       ntohs(my_host.sin_port));
				break;
			
			case RESP_EXIST:
				log_message(LOG_CONSOLE | LOG_WARNING,
                                                      "This username is taken");
				break;
			
			case RESP_BADUSR:
				log_message(LOG_CONSOLE | LOG_WARNING,
                                            "This username is badly formatted");
				break;
			
			default:
				flog_message(LOG_WARNING, "Unexpected server response: %s",
                                                              magic_name(resp));
		}
	} while ( resp != RESP_OK_LOGIN );
}

void play_shell() {
	char cmd[BUFFER_SIZE] = "";
	
	get_line(buffer, BUFFER_SIZE);
	sscanf(buffer, "%s", cmd); /*FIXME check return value */
	log_message(LOG_USERINPUT, buffer);
	
	if ( strcmp(buffer, "help") == 0 ) { /* -------------------------- > help */
		
		log_message(LOG_CONSOLE, "Commands: end, exit, help, hit, show, who");
		
	} else if ( strcmp(buffer, "who") == 0 ) { /* --------------------- > who */
		
		list_connected_clients();
		
	} else if ( strcmp(buffer, "end") == 0 ) { /* --------------------- > end */
		
		log_message(LOG_CONSOLE, "You surrendered!");
		end_match(TRUE);
		
	} else if ( strcmp(buffer, "exit") == 0 ) { /* ------------------- > exit */
		
		end_match(TRUE);
		shutdown(sock_server, SHUT_RDWR);
		close(sock_server);
		exit(EXIT_SUCCESS);
		
	} else if ( strcmp(cmd, "hit") == 0 ) { /* ------------------------ > hit */
		
		unsigned int cell;
		
		if ( sscanf(buffer, "hit %u", &cell) == 1 && cell >= 1 && cell <= 9 ) {
			
			if ( turn != my_player ) {
				log_message(LOG_CONSOLE, "It is not your turn");
				return;
			}
			
			if ( grid.cells[cell] != GAME_UNDEF ) {
				log_message(LOG_CONSOLE, "That cell is already taken");
				return;
			}
			
			make_move(cell, TRUE);
			
		} else log_message(LOG_CONSOLE, "Syntax: hit <n>, where n is 1-9");
		
	} else if ( strcmp(buffer, "show") == 0 ) { /* ------------------- > show */
		
		sprintgrid(buffer, &grid, "", BUFFER_SIZE);
		log_multiline(LOG_CONSOLE, buffer);
		if ( turn == my_player )
			flog_message(LOG_CONSOLE, "It is your turn (%c)", turn);
		else
			flog_message(LOG_CONSOLE, "It is %s's turn (%c)", opp_username, turn);
		
	} else if ( strcmp(buffer, "cheat") == 0 ) { /* ----------------- > cheat */
		
		int move;
		
		if ( turn != my_player ) {
			log_message(LOG_CONSOLE, "It is not your turn");
			return;
		}
		
		backtrack(&grid, my_player, &move);
		flog_message(LOG_CONSOLE, "Hitting on cell %d...", move);
		make_move(move, TRUE);
		
	} else if ( strcmp(buffer, "") != 0 ) {
		
		log_message(LOG_CONSOLE,
                           "Unknown command. Type 'help' for list of commands");
	}
}

bool say_hello() {
	int c, seed;
	
	log_message(LOG_DEBUG, "Going to say hello...");
	
	/* Generate seed */
	srand(time(NULL));
	c = (rand() % 102394) / 1059; /* c in [0, 96] */
	for ( ; c >= 0; c-- ) seed = rand();
	grid.seed = seed;
	update_hash(&grid);
	
	flog_message(LOG_DEBUG, "Seed is %08x", seed);
	
	pack(buffer, "bl", REQ_HELLO, seed);
	if ( send_buffer(opp_socket, buffer, 5) < 0 ) {
		log_error("Error send()");
		return FALSE;
	}
	
	return TRUE;
}

void got_hit_or_end() {
	uint8_t byte, move;
	uint32_t hash;
	int received;
	
	received = recv(opp_socket, buffer, 6, 0);
	flog_message(LOG_DEBUG, "Received=%d on line %d", received, __LINE__);
	
	if ( received < 0 ) {
		log_error("Error recv()");
		end_match(FALSE);
		return;
	}
	
	unpack(buffer, "bbl", &byte, &move, &hash);
	
	flog_message(LOG_DEBUG, "Got %s from player", magic_name(byte));
	
	if ( byte == REQ_HIT ) {
		
		if ( received != 6 ) {
			flog_message(LOG_WARNING, "Unexpected event on line %d", __LINE__);
			return;
		}
		
		if ( turn == my_player ) {
			flog_message(LOG_WARNING, "Unexpected event on line %d", __LINE__);
			return;
		}
		
		if ( move < 1 || move > 9 ) {
			flog_message(LOG_WARNING, "Unexpected event on line %d", __LINE__);
			return;
		}
		
		if ( grid.cells[move] != GAME_UNDEF ) {
			flog_message(LOG_WARNING, "Unexpected event on line %d", __LINE__);
			return;
		}
		
		make_move(move, FALSE);
		
		if ( grid.hash != hash ) {
			log_message(LOG_ERROR, "Hash mismatch");
			end_match(FALSE);
			/*TODO */
		}
		
	} else if ( byte == REQ_END ) {
		
		log_message(LOG_INFO, "Your opponent surrendered: you win!");
		end_match(FALSE);
		
	} else flog_message(LOG_WARNING, "Unexpected event on line %d: byte=%s",
                                                    __LINE__, magic_name(byte));
}

void got_play_request() {
	uint8_t length;
	
	my_state = BUSY;
	
	if ( recv(sock_server, &length, 1, 0) != 1 ) server_disconnected();
	/*FIXME check length */
	if ( recv(sock_server, buffer, length, 0) != length ) server_disconnected();
	
	buffer[length] = '\0';
	strcpy(opp_username, buffer);
	flog_message(LOG_INFO,
        "Got play request from [%s]. Accept (y) or refuse (n) ?", opp_username);
	
	do {
		get_line(buffer, BUFFER_SIZE);
		log_message(LOG_USERINPUT, buffer);

		if ( strcmp(buffer, "y") == 0 ) {
			
			if ( !connect_play_socket(NULL) ) {
				log_message(LOG_CONSOLE,
                               "Request automatically refused due to an error");
				
				if ( send_byte(sock_server, RESP_REFUSE) < 0 )
					server_disconnected();
				
				end_match(FALSE);
				return;
			}
			
			if ( send_byte(sock_server, RESP_OK_PLAY) < 0 )
				server_disconnected();
			
			my_state = BUSY;
			/*TODO */
			log_message(LOG_CONSOLE, "Request accepted. Waiting for connection "
                                                    "from the other client...");
			
			if ( get_hello() && connect_play_socket(&opp_host) )
				start_match(GAME_GUEST);
			else
				end_match(FALSE);
			
			break;
			
		} else if ( strcmp(buffer, "n") == 0 ) {
			
			if ( send_byte(sock_server, RESP_REFUSE) < 0 )
				server_disconnected();
			
			my_state = FREE;
			console->prompt = PROMPT_FREE;
			log_message(LOG_CONSOLE, "Request refused");
			break;
			
		} else {
			
			log_message(LOG_CONSOLE, "Accept (y) or refuse (n) ?");
			
		}
	} while (TRUE);
}

bool get_hello() {
	uint8_t byte;
	uint32_t seed;
	int received;
	socklen_t addrlen = sizeof(opp_host);
	
	if ( poll_in(opp_socket, HELLO_TIMEOUT) <= 0 ) {
		log_message(LOG_CONSOLE,
                  "Timeout while waiting for connection from the other client");
		return FALSE;
	}
	
	received = recvfrom(opp_socket, buffer, 5, 0, (struct sockaddr*) &opp_host,
                                                                      &addrlen);
	
	if ( received != 5 ) return FALSE;
	
	unpack(buffer, "bl", &byte, &seed);
	
	print_ip(opp_host);
	flog_message(LOG_DEBUG, "Got %s from %s:%hu", magic_name(byte), buffer,
                                                      ntohs(opp_host.sin_port));
	
	if ( byte != REQ_HELLO ) return FALSE;
	
	grid.seed = seed;
	update_hash(&grid);
	
	flog_message(LOG_DEBUG, "Seed is %08x", seed);
	
	return TRUE;
}

void get_play_response() {
	uint8_t resp;
	uint16_t opp_udp_port;
	int received;
	
	if ( poll_in(sock_server, PLAY_RESPONSE_TIMEOUT) <= 0 ) {
		log_message(LOG_INFO, "Timeout while waiting for play response");
		end_match(FALSE);
		return;
	}
	
	if ( recv(sock_server, &resp, 1, 0) != 1 )
		server_disconnected();
	
	switch ( resp ) {
		case RESP_OK_PLAY:
			received = recv(sock_server, buffer, sizeof(struct in_addr) + 2, 0);
			if ( received != sizeof(struct in_addr) + 2 )
				server_disconnected();
			
			opp_host.sin_family = AF_INET;
			memset(opp_host.sin_zero, 0, sizeof(opp_host.sin_zero));
			unpack(buffer, "lw", &(opp_host.sin_addr), &opp_udp_port);
			print_ip(opp_host);
			opp_host.sin_port = htons(opp_udp_port);
			
			flog_message(LOG_INFO,
                    "[%s] accepted to play with you. Contacting host %s:%hu...",
                                            opp_username, buffer, opp_udp_port);
			
			if ( connect_play_socket(&opp_host) && say_hello() ) {
				start_match(GAME_HOST);
				return;
			}
			
			log_message(LOG_INFO, "Cannot connect to client, go back to FREE");
			
			end_match(FALSE);
			
			break;
		
		case RESP_REFUSE:
			flog_message(LOG_INFO, "[%s] refused to play", opp_username);
			break;
		
		case RESP_NONEXIST:
			flog_message(LOG_INFO, "[%s] does not exist", opp_username);
			break;
		
		case RESP_BUSY:
			flog_message(LOG_INFO, "[%s] is occupied in another match",
                                                                  opp_username);
			break;
		
		default:
			flog_message(LOG_WARNING, "Unexpected server response: %s",
                                                              magic_name(resp));
	}
	
	opp_username[0] = '\0';
	my_state = FREE;
}

void list_connected_clients() {
	uint8_t resp, length;
	uint32_t count, i;

	buffer[0] = REQ_WHO;
	
	if ( send_byte(sock_server, REQ_WHO) < 0 ) server_disconnected();
	if ( recv(sock_server, &resp, 1, 0) != 1 ) server_disconnected();
	
	if ( resp != RESP_WHO ) {
		flog_message(LOG_WARNING, "Unexpected server response: %s",
                                                              magic_name(resp));
		return;
	}
	
	if ( recv(sock_server, buffer, 4, 0) != 4 ) server_disconnected();
	unpack(buffer, "l", &count);
	if (count > 100) exit(EXIT_FAILURE); /*FIXME debug statement */
	
	flog_message(LOG_CONSOLE, "There are %u connected clients:", count);
	for (i = 0; i < count; i++) {
		if ( recv(sock_server, &length, 1, 0) != 1 )
			server_disconnected();
		
		if ( recv(sock_server, buffer, length, 0) != length )
			server_disconnected();
		
		buffer[length] = '\0';
		
		if ( strcmp(buffer, my_username) == 0 )
			flog_message(LOG_CONSOLE, "[%s] <-- It's you!", buffer);
		else
			flog_message(LOG_CONSOLE, "[%s]", buffer);
	}
}

void make_move(unsigned int cell, bool send_opp) {
	char winner;
	
	grid.cells[cell] = turn;
	update_hash(&grid);
	winner = get_winner(&grid);
	turn = inverse(turn);
	flog_message(LOG_DEBUG, "Hash is %08x", grid.hash);
	
	if ( send_opp ) {
		pack(buffer, "bbl", REQ_HIT, (uint8_t) cell, grid.hash);
		if ( send_buffer(opp_socket, buffer, 6) < 0 )
			log_error("Error send()");
	}
	
	if ( turn == my_player )
		flog_message(LOG_INFO, "[%s] hit on cell %d", opp_username, cell);
	else
		flog_message(LOG_CONSOLE, "You hit on cell %d", cell);
	
	
	if ( winner == GAME_UNDEF ) {
		if ( turn == my_player ) flog_message(LOG_CONSOLE, "It's your turn (%c)", turn);
		else flog_message(LOG_CONSOLE, "It is %s's turn (%c)", opp_username, turn);
		
		return;
	}
	
	else if ( winner == my_player ) log_message(LOG_INFO, "You won!");
	else if ( winner == inverse(my_player) ) log_message(LOG_INFO, "You lost!");
	else if ( winner == GAME_DRAW ) log_message(LOG_INFO, "Draw!");
	else return;
	
	/* if ( winner != GAME_UNDEF ) */
	end_match(FALSE);
}

void send_play_request() {
	uint8_t username_length;
	
	username_length = (uint8_t) strlen(opp_username);
	pack(buffer, "bbs", REQ_PLAY, username_length, opp_username);
	if ( send_buffer(sock_server, buffer, 2 + username_length) < 0 )
		server_disconnected();
	
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

void start_match(char me) {
	my_state = PLAY;
	log_statechange();
	my_player = me;
	turn = GAME_HOST;
	init_grid(&grid);
	monitor_socket_r(opp_socket);
	
	console->prompt = PROMPT_PLAY;
	switch ( me ) {
		case GAME_HOST:
			flog_message(LOG_CONSOLE, "Match has started. It's your turn (%c)",
                                                                          turn);
			break;
			
		case GAME_GUEST:
			flog_message(LOG_CONSOLE, "Match has started. It is %s's turn (%c)",
                                                            opp_username, turn);
			break;
			
		default:
			flog_message(LOG_WARNING, "Unexpected event on line %d", __LINE__);
	}
}

int poll_in(int socket, int timeout) {
	int poll_ret;
	struct pollfd pfd = {0, POLLIN, 0};
	pfd.fd = socket;
	
	poll_ret = poll(&pfd, 1, timeout);
	
	if ( poll_ret < 0 )
		log_error("Error poll()");
	else if ( poll_ret > 0 && pfd.revents != POLLIN )
		flog_message(LOG_WARNING | LOG_ERROR, "Unexpected event on line %d",
                                                                      __LINE__);
	return poll_ret;
}
