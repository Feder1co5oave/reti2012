#include <stdio.h>
#include <stdlib.h>
#include <time.h>
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
#include "tris_game.h"

#define update_maxfds(n) maxfds = (maxfds < (n) ? (n) : maxfds)
#define monitor_socket_r(sock) { FD_SET(sock, &readfds); update_maxfds(sock); }
#define monitor_socket_w(sock) { FD_SET(sock, &writefds); update_maxfds(sock); }
#define unmonitor_socket_r(sock) FD_CLR(sock, &readfds)
#define unmonitor_socket_w(sock) FD_CLR(sock, &writefds)
#define log_statechange() \
                 flog_message(LOG_DEBUG, _("I am now %s"), state_name(my_state))

#define print_ip(host) \
        inet_ntop(AF_INET, &(host.sin_addr), buffer, sizeof(struct sockaddr_in))



/* ===[ Helpers ]============================================================ */

bool connect_play_socket(void);
void end_match(void);
void free_shell(void);
bool get_hello(void);
void get_play_response(void);
void got_hit(void);
void got_play_request(void);
void list_connected_clients(void);
void login(void);
void make_move(unsigned int cell);
bool open_play_socket(void);
void play_shell(void);
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
char player;



/* ===[ Main ]=============================================================== */

int main (int argc, char **argv) {
	struct timeval tv = DEFAULT_TIMEOUT_INIT, _tv;
	struct sockaddr_in server_host;
	socklen_t sockaddrlen = sizeof(struct sockaddr_in);
	fd_set _readfds, _writefds;
	int sel_status;
	
	char *_setlocale, *_bindtextdomain, *_textdomain;
	
	/* Set localization */
	_setlocale = setlocale(LC_ALL, "");
	_bindtextdomain = bindtextdomain("tris", "locale");
	_textdomain = textdomain("tris");

	/* Set log files */
	console = new_log(stdout, LOG_CONSOLE | LOG_INFO | LOG_ERROR, FALSE);
	sprintf(buffer, "tris_client-%d.log", getpid());
	open_log(buffer, LOG_ALL);
	
	if (_setlocale ) flog_message(LOG_DEBUG, _("Locate set to %s"), _setlocale);
	else log_message(LOG_WARNING, _("Cannot set locale"));
	if ( _bindtextdomain ) flog_message(LOG_DEBUG, _("Message catalogs directory set to %s"), _bindtextdomain);
	else log_message(LOG_WARNING, _("Cannot set message catalogs directory"));
	if ( _textdomain ) flog_message(LOG_DEBUG, _("Gettext domain set to %s"), _textdomain);
	else log_message(LOG_WARNING, _("Cannot set gettext domain"));
	
	if (argc != 3) {
		flog_message(LOG_CONSOLE, _("Usage: %s <server_ip> <server_port>"),
                                                                       argv[0]);
		exit(EXIT_FAILURE);
	}
	
	if ( inet_pton(AF_INET, argv[1], &(server_host.sin_addr)) != 1 ) {
		log_message(LOG_CONSOLE, _("Invalid server address"));
		exit(EXIT_FAILURE);
	}
	
	server_host.sin_family = AF_INET;
	server_host.sin_port = htons((uint16_t) atoi(argv[2])); /*FIXME check cast */
	memset(server_host.sin_zero, 0, sizeof(server_host.sin_zero));

	sock_server = socket(server_host.sin_family, SOCK_STREAM , 0);
	if ( sock_server == -1 ) {
		log_error(_("Error socket()"));
		exit(EXIT_FAILURE);
	}
	
	if ( connect(sock_server, (struct sockaddr*) &server_host,
                                                   sizeof(server_host)) != 0 ) {
		log_error(_("Error connect()"));
		exit(EXIT_FAILURE);
	}
	
	if ( getsockname(sock_server, (struct sockaddr*) &my_host, &sockaddrlen) !=
                               0 || sockaddrlen > sizeof(struct sockaddr_in) ) {
		
		log_error(_("Error getsockname()"));
		exit(EXIT_FAILURE);
	}
	
	print_ip(my_host);
	flog_message(LOG_DEBUG, _("I am host %s:%hu"), buffer, ntohs(my_host.sin_port));
	
	my_state = CONNECTED;
	log_message(LOG_CONSOLE, _("Connected to the server"));
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
	_tv = tv;
	
	
	while ( (sel_status = select(maxfds + 1, &_readfds, &_writefds, NULL, &_tv)) >= 0 ) {
		
		if ( sel_status == 0 ) {
			flog_message(LOG_DEBUG, _("Select() timed out while %s"),
                                                          state_name(my_state));
			
			if ( my_state == PLAY ) end_match();
			else {
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
					flog_message(LOG_WARNING,
                  _("Got unwanted user input while %s:"), state_name(my_state));
					log_message(LOG_USERINPUT, buffer);
					log_message(LOG_ERROR, ""); /*FIXME */
			}
			
		} else if ( FD_ISSET(sock_server, &_readfds) ) {
			
			uint8_t cmd;
			
			if ( recv(sock_server, &cmd, 1, 0) != 1 )
				server_disconnected();
						
			if ( cmd == REQ_PLAY ) {
					flog_message(LOG_DEBUG,
                                         _("Got REQ_PLAY from server while %s"),
                                                          state_name(my_state));
					
					if ( my_state == FREE )
						got_play_request();
					else if ( send_byte(sock_server, RESP_REFUSE) < 0 )
						server_disconnected();
			} else {
				flog_message(LOG_WARNING, _("Unexpected server cmd: %s"),
                                                               magic_name(cmd));
				
				if ( send_byte(sock_server, RESP_BADREQ) < 0 )
					server_disconnected();
				/*FIXME */
			}
			
		} else if ( my_state == PLAY && FD_ISSET(opp_socket, &_readfds) ) {
			
			got_hit();
			
		} else {
			/*FIXME */
			flog_message(LOG_WARNING, _("Unexpected event on line %d while %s"),
                                                __LINE__, state_name(my_state));
			
			sleep(1);
		}
		
		_readfds = readfds;
		_writefds = writefds;
		_tv = tv;
	}

	log_error(_("Error select()"));
	shutdown(sock_server, SHUT_RDWR);
	/*TODO get rid of all the shutdown() calls */
	close(sock_server);
	exit(EXIT_FAILURE);
}

/* ========================================================================== */

void free_shell() {
	int line_length;
	char cmd[BUFFER_SIZE] = "";
	
	line_length = get_line(buffer, BUFFER_SIZE);
	sscanf(buffer, "%s", cmd); /*FIXME check return value */
	log_message(LOG_USERINPUT, buffer);
	
	if ( strcmp(buffer, "help") == 0 ) { /* -------------------------- > help */
		
		log_message(LOG_CONSOLE, _("Commands: exit, help, play, who"));
		
	} else if ( strcmp(buffer, "who") == 0 ) { /* --------------------- > who */
		
		list_connected_clients();
		
	} else if ( strcmp(cmd, "play") == 0 ) { /* ---------------------- > play */
		
		char username[50];
		int s, username_length;
		
		s = sscanf(buffer, "play %s", username);
		username_length = strlen(username);
		
		if ( username_length <= 0 || s != 1 ) {
			log_message(LOG_CONSOLE, _("Syntax: play <player>"));
			return;
		}
		
		if ( username_length > MAX_USERNAME_LENGTH ) {
			log_message(LOG_CONSOLE, _("This username is too long"));
			return;
		}
		
		if ( username_length < MIN_USERNAME_LENGTH ) {
			log_message(LOG_CONSOLE, _("This username is too short"));
			return;
		}
		
		if ( !username_is_valid(username, username_length) ) {
			log_message(LOG_CONSOLE, _("This username is badly formatted"));
			return;
		}
		
		if ( strcmp(username, my_username) == 0 ) {
			log_message(LOG_CONSOLE, _("You cannot play with yourself"));
			return;
		}
		
		strcpy(opp_username, username);
		send_play_request();
		get_play_response();
		
	} else if ( strcmp(buffer, "exit") == 0 ) { /* ------------------- > exit */
		
		shutdown(sock_server, SHUT_RDWR);
		close(sock_server);
		exit(EXIT_SUCCESS);
		
	} else if ( strcmp(buffer, "") == 0 ) { /* ---------------------------- > */
		
		log_prompt(console);
		
	} else {
		
		log_message(LOG_CONSOLE, _("Unknown command"));
		
	}
}

bool open_play_socket() {
	opp_host.sin_family = AF_INET;
	opp_socket = socket(opp_host.sin_family, SOCK_DGRAM, 0);
	if ( opp_socket == -1 ) {
		log_error(_("Error socket(SOCK_DGRAM)"));
		return FALSE;
	}
	
	log_message(LOG_DEBUG, _("Opened opp_socket"));
	
	if ( bind(opp_socket, (struct sockaddr*) &my_host, sizeof(my_host)) != 0 ) {
		
		log_error(_("Error bind(SOCK_DGRAM)"));
		opp_socket = -1;
		return FALSE;
	}
	
	print_ip(my_host);
	flog_message(LOG_DEBUG, _("Bound opp_socket to %s:%hu"), buffer,
                                                       ntohs(my_host.sin_port));
	
	return TRUE;
}

bool connect_play_socket() {
	if ( connect(opp_socket, (struct sockaddr*) &opp_host, sizeof(opp_host)) !=
                                                                           0 ) {
		log_error(_("Error connect(SOCK_DGRAM)"));
		opp_socket = -1;
		return FALSE;
	}
	
	print_ip(opp_host);
	flog_message(LOG_DEBUG, _("Connected opp_socket to %s:%hu"), buffer,
                                                      ntohs(opp_host.sin_port));
	
	return TRUE;
}

void end_match() {
	uint8_t resp;
	
	/*TODO send REQ_END to opp */
	
	if ( send_byte(sock_server, REQ_END) < 0 ) server_disconnected();
	if ( recv(sock_server, &resp, 1, 0) != 1 ) server_disconnected();
	if ( resp == RESP_OK_FREE ) {
		log_message(LOG_CONSOLE, _("End of match. You are now free."));
	} else {
		flog_message(LOG_WARNING, _("Unexpected server response: %s"),
                                                              magic_name(resp));
	}
	
	my_state = FREE;
	log_statechange();
	console->prompt = '>';
	log_prompt(console);
	
	unmonitor_socket_r(opp_socket);
	opp_socket = -1;
	memset(&opp_host, 0, sizeof(opp_host));
	opp_username[0] = '\0';
}

void login() {
	int username_length;
	uint8_t resp;
	unsigned int my_udp_port;
	
	do {
		log_message(LOG_CONSOLE, _("Insert your username:"));
		username_length = get_line(buffer, BUFFER_SIZE);
		log_message(LOG_USERINPUT, buffer);
		
		if ( username_length > MAX_USERNAME_LENGTH ) {
			log_message(LOG_CONSOLE, _("This username is too long"));
			continue;
		}
		
		if ( username_length < MIN_USERNAME_LENGTH ) {
			log_message(LOG_CONSOLE, _("This username is too short"));
			continue;
		}
		
		if ( !username_is_valid(buffer, username_length) ) {
			log_message(LOG_CONSOLE, _("This username is badly formatted"));
			continue;
		}
		
		strcpy(my_username, buffer);
		
		do {
			log_message(LOG_CONSOLE, _("Insert your UDP port:"));
			get_line(buffer, BUFFER_SIZE);
			log_message(LOG_USERINPUT, buffer);
			if ( sscanf(buffer, " %u", &my_udp_port) != 1 ) continue;
			
			if ( my_udp_port >= (1 << 16) ) {
				log_message(LOG_CONSOLE,
                                       _("Your UDP port is not in port range"));
				
				continue;
			}
			
			if ( my_udp_port < (1 << 10) )
				log_message(LOG_CONSOLE,
                  _("Your UDP port is in the system range, it might not work"));
			
			break;
		} while (TRUE);
		
		pack(buffer, "bbsw", REQ_LOGIN, (uint8_t) username_length,
                                           my_username, (uint16_t) my_udp_port);
		
		my_host.sin_port = htons((uint16_t) my_udp_port);
		
		if ( send_buffer(sock_server, buffer, 4 + username_length) < 0 )
			server_disconnected();
		
		if ( recv(sock_server, &resp, 1, 0) != 1 )
			server_disconnected();
		
		switch ( resp ) {
			case RESP_OK_LOGIN:
				my_state = FREE;
				flog_message(LOG_CONSOLE, _("Successfully logged in as [%s]"),
                                                                   my_username);
				break;
			
			case RESP_EXIST:
				log_message(LOG_CONSOLE, _("This username is taken"));
				break;
			
			case RESP_BADUSR:
				log_message(LOG_CONSOLE, _("This username is badly formatted"));
				break;
			
			default:
				flog_message(LOG_WARNING, _("Unexpected server response: %s"),
                                                              magic_name(resp));
		}
	} while ( resp != RESP_OK_LOGIN );
}

void play_shell() {
	int line_length, cmd_length;
	char cmd[BUFFER_SIZE] = "";
	
	line_length = get_line(buffer, BUFFER_SIZE);
	sscanf(buffer, "%s", cmd); /*FIXME check return value */
	cmd_length = strlen(cmd);
	log_message(LOG_USERINPUT, buffer);
	
	if ( strcmp(buffer, "help") == 0 ) { /* -------------------------- > help */
		
		log_message(LOG_CONSOLE,
                                _("Commands: end, exit, help, hit, show, who"));
		
	} else if ( strcmp(buffer, "who") == 0 ) { /* --------------------- > who */
		
		list_connected_clients();
		
	} else if ( strcmp(buffer, "end") == 0 ) { /* --------------------- > end */
		
		end_match();
		
	} else if ( strcmp(buffer, "exit") == 0 ) { /* ------------------- > exit */
		
		/*TODO end_match(); */
		shutdown(sock_server, SHUT_RDWR);
		close(sock_server);
		exit(EXIT_SUCCESS);
		
	} else if ( strcmp(cmd, "hit") == 0 ) { /* ------------------------ > hit */
		
		unsigned int cell;
		
		if ( sscanf(buffer, "hit %1u", &cell) == 1 && cell >= 1 && cell <= 9 ) {
			if ( grid.cells[cell] != GAME_UNDEF ) {
				log_message(LOG_CONSOLE, _("That cell is already taken"));
				return;
			}
			
			make_move(cell);
			
		} else log_message(LOG_CONSOLE, _("Syntax: hit <n>, where n is 1-9"));
		
	} else if ( strcmp(buffer, "show") == 0 ) { /* ------------------- > show */
		
		sprintgrid(buffer, &grid, "", BUFFER_SIZE);
		console->prompt = FALSE;
		log_multiline(LOG_CONSOLE, buffer);
		console->prompt = '#';
		log_prompt(console);
		
	} else if ( strcmp(buffer, "") == 0 ) { /* ---------------------------- > */
		
		log_prompt(console);
		
	} else {
		
		log_message(LOG_CONSOLE, _("Unknown command"));
		
	}
}

bool say_hello() {
	int c, salt;
	
	log_message(LOG_DEBUG, _("Going to say hello..."));
	
	/* Generate salt */
	srand(time(NULL));
	c = (rand() % 102394) / 1059; /* c in [0, 96] */
	for ( ; c >= 0; c-- ) salt = rand();
	grid.salt = salt;
	update_hash(&grid);
	
	flog_message(LOG_DEBUG, _("Salt is %08x"), salt);
	
	pack(buffer, "bl", REQ_HELLO, salt);
	if ( send_buffer(opp_socket, buffer, 5) < 0 ) {
		log_error(_("Error send()"));
		return FALSE;
	}
	
	return TRUE;
}

void got_hit() {
	uint8_t byte, move;
	uint32_t hash;
	int received;
	
	received = recv(opp_socket, buffer, 6, 0);
	flog_message(LOG_DEBUG, _("Received=%d on line %d"), received, __LINE__);
	if ( received == 0 ) return;
	if ( received != 6 ) log_error(_("Error recv()"));
	
	
	/* if ( received == 0 ) return; */
	
	unpack(buffer, "bbl", &byte, &move, &hash);
	
	flog_message(LOG_DEBUG, _("Got %s from player"), magic_name(byte));
	
	if ( byte == REQ_HIT ) {
		if ( move >= 1 && move <= 9 ) {
			if ( grid.cells[move] == GAME_UNDEF ) {
				
				grid.cells[move] = inverse(player);
				update_hash(&grid);
				flog_message(LOG_DEBUG, _("Hash is %08x"), grid.hash);
				
				if ( grid.hash != hash ) {
					log_message(LOG_ERROR, _("Hash mismatch"));
				} else {
					flog_message(LOG_INFO, _("[%s] hit on cell %d"),
                                                            opp_username, move);
				}
				
			} else flog_message(LOG_WARNING, _("Unexpected event on line %d"),
                                                                      __LINE__);
			
		} else flog_message(LOG_WARNING, _("Unexpected event on line %d"),
                                                                      __LINE__);
		
	} else flog_message(LOG_WARNING, _("Unexpected event on line %d: byte=%s"),
                                                    __LINE__, magic_name(byte));
}

void got_play_request() {
	uint8_t length;
	int line_length;
	
	my_state = BUSY;
	
	if ( recv(sock_server, &length, 1, 0) != 1 ) server_disconnected();
	if ( recv(sock_server, buffer, length, 0) != length ) server_disconnected();
	
	buffer[length] = '\0';
	flog_message(LOG_INFO,
           _("Got play request from [%s]. Accept (y) or refuse (n) ?"), buffer);
	
	do {
		line_length = get_line(buffer, BUFFER_SIZE);

		if ( strcmp(buffer, _("y")) == 0 ) {
			
			if ( open_play_socket() ) {
				if ( send_byte(sock_server, RESP_OK_PLAY) < 0 )
					server_disconnected();
				
				my_state = BUSY;
				console->prompt = FALSE;
				/*TODO */
				log_message(LOG_CONSOLE,
        _("Request accepted. Waiting for connection from the other client..."));
				
				if ( get_hello() && connect_play_socket() ) {
					start_match(GAME_GUEST);
					return;
				}
			} /*TODO else */
			
			log_message(LOG_CONSOLE,
                                _("Cannot connect to client, go back to FREE"));
			my_state = FREE;
			break;
			
		} else if ( strcmp(buffer, _("n")) == 0 ) {
			
			if ( send_byte(sock_server, RESP_REFUSE) < 0 )
				server_disconnected();
			
			my_state = FREE;
			log_message(LOG_CONSOLE, _("Request refused"));
			break;
			
		} else {
			
			log_message(LOG_CONSOLE, _("Accept (y) or refuse (n) ?"));
			
		}
	} while (TRUE);
}

bool get_hello() {
	uint8_t byte;
	uint32_t salt;
	int received;
	socklen_t addrlen = sizeof(opp_host);
	
	received = recvfrom(opp_socket, buffer, 5, 0, (struct sockaddr*) &opp_host,
                                                                      &addrlen);
	
	if ( received != 5 ) return FALSE;
	
	unpack(buffer, "bl", &byte, &salt);
	
	print_ip(opp_host);
	flog_message(LOG_DEBUG, _("Got %s from %s:%hu"), magic_name(byte), buffer,
                                                      ntohs(opp_host.sin_port));
	
	if ( byte != REQ_HELLO ) return FALSE;
	
	grid.salt = salt;
	update_hash(&grid);
	
	flog_message(LOG_DEBUG, _("Salt is %08x"), salt);
	
	return TRUE;
}

void get_play_response() {
	uint8_t resp;
	uint16_t opp_udp_port;
	int received;
	
	/*TODO set timeout */
	
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
			
			flog_message(LOG_CONSOLE,
                 _("[%s] accepted to play with you. Contacting host %s:%hu..."),
                                            opp_username, buffer, opp_udp_port);
			
			if ( open_play_socket() && connect_play_socket() && say_hello() ) {
				start_match(GAME_HOST);
				return;
			} else log_message(LOG_CONSOLE,
                                _("Cannot connect to client, go back to FREE"));
			
			break;
		
		case RESP_REFUSE:
			flog_message(LOG_CONSOLE, _("[%s] refused to play"), opp_username);
			break;
		
		case RESP_NONEXIST:
			flog_message(LOG_CONSOLE, _("[%s] does not exist"), opp_username);
			break;
		
		case RESP_BUSY:
			flog_message(LOG_CONSOLE, _("[%s] is occupied in another match"),
                                                                  opp_username);
			break;
		
		default:
			flog_message(LOG_WARNING, _("Unexpected server response: %s"),
                                                              magic_name(resp));
	}
	
	my_state = FREE;
	console->prompt = '>';
	log_prompt(console);
}

void list_connected_clients() {
	uint8_t resp, length;
	uint32_t count, i;
	char oldprompt;

	buffer[0] = REQ_WHO;
	
	if ( send_byte(sock_server, REQ_WHO) < 0 ) server_disconnected();
	if ( recv(sock_server, &resp, 1, 0) != 1 ) server_disconnected();
	
	switch ( resp ) {
		case RESP_WHO:
			if ( recv(sock_server, buffer, 4, 0) != 4 ) server_disconnected();
			unpack(buffer, "l", &count);
			if (count > 100) exit(EXIT_FAILURE); /*FIXME debug statement */
			
			oldprompt = console->prompt;
			console->prompt = FALSE;
			flog_message(LOG_CONSOLE, _("There are %u connected clients"),
                                                                         count);
			for (i = 0; i < count; i++) {
				if ( recv(sock_server, &length, 1, 0) != 1 )
					server_disconnected();
				
				if ( recv(sock_server, buffer, length, 0) != length )
					server_disconnected();
				
				buffer[length] = '\0';
				flog_message(LOG_CONSOLE, _("[%s]"), buffer);
			}
			console->prompt = oldprompt;
			log_prompt(console);
			break;
		
		default:
			flog_message(LOG_WARNING, _("Unexpected server response: %s"),
                                                              magic_name(resp));
	}
}

void make_move(unsigned int cell) {
	grid.cells[cell] = player;
	update_hash(&grid);
	pack(buffer, "bbl", REQ_HIT, (uint8_t) cell, grid.hash);
	if ( send_buffer(opp_socket, buffer, 6) < 0 )
		log_error(_("Error send()"));
}

void send_play_request() {
	uint8_t username_length;
	
	username_length = (uint8_t) strlen(opp_username);
	pack(buffer, "bbs", REQ_PLAY, username_length, opp_username);
	if ( send_buffer(sock_server, buffer, 2 + username_length) < 0 )
		server_disconnected();
	
	console->prompt = FALSE;
	flog_message(LOG_CONSOLE,
         _("Sent play request to [%s], waiting for response..."), opp_username);
	
	my_state = BUSY;
}

void server_disconnected() {
	log_message(LOG_ERROR, _("Lost connection to server"));
	shutdown(sock_server, SHUT_RDWR);
	close(sock_server);
	/*TODO if ( state == PLAYING ) */
	exit(EXIT_FAILURE);
}

void start_match(char me) {
	my_state = PLAY;
	console->prompt = '#';
	log_prompt(console);
	player = me;
	monitor_socket_r(opp_socket);
	/*TODO */
}
