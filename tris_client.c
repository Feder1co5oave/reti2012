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
#define log_statechange() \
                    flog_message(LOG_DEBUG, "I am now %s", state_name(my_state))

#define print_ip(host) \
        inet_ntop(AF_INET, &(host.sin_addr), buffer, sizeof(struct sockaddr_in))



/* ===[ Helpers ]============================================================ */

void free_shell(void);
void got_play_request(void);
void get_play_response(void);
void list_connected_clients(void);
void login(void);
void play_shell(void);
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
	struct timeval tv = DEFAULT_TIMEOUT_INIT, _tv;
	struct sockaddr_in server_host;
	int received;
	fd_set _readfds, _writefds;
	int sel_status;

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
	_tv = tv;
	
	
	while ( (sel_status = select(maxfds + 1, &_readfds, &_writefds, NULL, &_tv)) >= 0 ) {
		uint8_t cmd;
		
		if ( sel_status == 0 ) {
			flog_message(LOG_DEBUG, "Select() timed out while %s",
                                                          state_name(my_state));
			if ( my_state == PLAY ) /* TODO end_match(); */;
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
					flog_message(LOG_WARNING, "Got unwanted user input while %s:", state_name(my_state));
					log_message(LOG_USERINPUT, buffer);
					log_message(LOG_ERROR, "");
			}
			
		} else if ( my_state != NONE && FD_ISSET(sock_server, &_readfds) ) {
			received = recv(sock_server, &cmd, 1, 0);
			if ( received != 1 ) server_disconnected();
			
			switch ( cmd ) {
				case REQ_PLAY:
					if ( my_state == FREE )
						got_play_request();
					else if ( send_byte(sock_server, RESP_REFUSE) < 0 )
						server_disconnected();
					
					break;
				
				case RESP_OK_PLAY:
					/*TODO ? */
				
				default:
					flog_message(LOG_WARNING, "Unexpected server response: %s",
                                                               magic_name(cmd));
					
					if ( send_byte(sock_server, RESP_BADREQ) < 0 )
						log_error("Error send()");
					/*FIXME */
			}
		} else if ( my_state == PLAY && FD_ISSET(opp_socket, &_readfds) ) {
			/*TODO */
		} else {
			/*FIXME */
			flog_message(LOG_WARNING, "Unexpected event on line %d", __LINE__);
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
	int line_length, cmd_length;
	char cmd[BUFFER_SIZE];
	
	line_length = get_line(buffer, BUFFER_SIZE);
	
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
		
	} else if ( strcmp(buffer, "exit") == 0 ) {
		
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
	int p;
	int username_length;
	uint8_t resp;
	
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
		
		strcpy(my_username, buffer);
		my_username_length = username_length;
		
		log_message(LOG_CONSOLE, "Insert your UDP port:");
		get_line(buffer, BUFFER_SIZE);
		log_message(LOG_USERINPUT, buffer);
		p = sscanf(buffer, "%hu", &my_udp_port); /*FIXME if ( p != 1 ) */
		
		if ( my_udp_port < 1024 )
			log_message(LOG_CONSOLE,
                     "Your UDP port is in the system range, it might not work");
		
		pack(buffer, "bbsw", REQ_LOGIN, (uint8_t) my_username_length,
                                                      my_username, my_udp_port);
		
		my_host.sin_port = htons(my_udp_port);
		
		if ( send_buffer(sock_server, buffer, 4 + my_username_length) < 0 )
			server_disconnected();
		
		if ( recv(sock_server, &resp, 1, 0) != 1 )
			server_disconnected();
		
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

void play_shell() {
	int line_length, cmd_length;
	char cmd[BUFFER_SIZE] = "";
	
	line_length = get_line(buffer, BUFFER_SIZE);
	sscanf(buffer, "%s", cmd); /*FIXME check return value */
	cmd_length = strlen(cmd);
	log_message(LOG_USERINPUT, buffer);
	
	if ( strcmp(buffer, "help") == 0 || strcmp(buffer, "?") == 0 ) {
		
		log_message(LOG_CONSOLE, "Commands: help, who, hit, map, end, exit");
		
	} else if ( strcmp(buffer, "who") == 0 ) {
		
		list_connected_clients();
		
	} else if ( strcmp(buffer, "end") == 0 ) {
		
		/*TODO end_match();	*/
		
	} else if ( strcmp(buffer, "exit") == 0 ) {
		
		/*TODO end_match(); */
		shutdown(sock_server, SHUT_RDWR);
		close(sock_server);
		exit(EXIT_SUCCESS);
		
	} else if ( strcmp(cmd, "hit") == 0 ) {
		
		unsigned int cell;
		
		if ( sscanf(buffer + cmd_length, "%1u", &cell) == 1 && cell >= 1 && cell
                                                                        <= 9 ) {
			/*TODO */
		} else log_message(LOG_CONSOLE, "Syntax: hit n, where n is 1-9");
		
	} else if ( strcmp(buffer, "") == 0 ) {
		
		log_prompt(console);
		
	} else {
		
		log_message(LOG_CONSOLE, "Unknown command");
		
	}
}

void got_play_request() {
	uint8_t length;
	int line_length;
	
	my_state = BUSY;
	
	if ( recv(sock_server, &length, 1, 0) != 1 ) server_disconnected();
	if ( recv(sock_server, buffer, length, 0) != length ) server_disconnected();
	
	buffer[length] = '\0';
	flog_message(LOG_INFO, "Got play request from [%s]. Accept (y) or refuse (n) ?", buffer);
	
	do {
		line_length = get_line(buffer, BUFFER_SIZE);

		if ( strcmp(buffer, "y") == 0 ) {
			
			if ( send_byte(sock_server, RESP_OK_PLAY) < 0 )
				server_disconnected();
			
			my_state = PLAY;
			/*TODO */
			log_message(LOG_CONSOLE, "Request accepted. Waiting for connection "
                                                    "from the other client...");
			break;
			
		} else if ( strcmp(buffer, "n") == 0 ) {
			
			if ( send_byte(sock_server, RESP_REFUSE) < 0 )
				server_disconnected();
			
			my_state = FREE;
			log_message(LOG_CONSOLE, "Request refused");
			break;
			
		} else {
			
			log_message(LOG_CONSOLE, "Accept (y) or refuse (n) ?");
			
		}
	} while (TRUE);
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
			print_ip(opp_host);
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
			flog_message(LOG_CONSOLE, "[%s] is occupied in another match",
                                                                  opp_username);
			my_state = FREE;
			break;
		
		default:
			flog_message(LOG_WARNING, "Unexpected server response: %s",
                                                              magic_name(resp));
	}
	
	console->prompt = '>';
	log_prompt(console);
}

void list_connected_clients() {
	uint8_t resp, length;
	uint32_t count, i;

	buffer[0] = REQ_WHO;
	
	if ( send_byte(sock_server, REQ_WHO) < 0 ) server_disconnected();
	if ( recv(sock_server, &resp, 1, 0) != 1 ) server_disconnected();
	
	switch ( resp ) {
		case RESP_WHO:
			if ( recv(sock_server, buffer, 4, 0) != 4 ) server_disconnected();
			unpack(buffer, "l", &count);
			if (count > 100) exit(EXIT_FAILURE); /*FIXME debug statement */

			console->prompt = FALSE;
			flog_message(LOG_CONSOLE, "There are %u connected clients", count);
			for (i = 0; i < count; i++) {
				if ( recv(sock_server, &length, 1, 0) != 1 )
					server_disconnected();
				
				if ( recv(sock_server, buffer, length, 0) != length )
					server_disconnected();
				
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
	
	console->prompt = FALSE;
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
