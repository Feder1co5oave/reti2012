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

#include "common.h"
#include "pack.h"
#include "client_list.h"
#include "log.h"

#define BACKLOG 5

#define update_maxfds(n) maxfds = (maxfds < (n) ? (n) : maxfds)
#define monitor_socket_r(sock) { FD_SET(sock, &readfds); update_maxfds(sock); }
#define monitor_socket_w(sock) { FD_SET(sock, &writefds); update_maxfds(sock); }
#define unmonitor_socket_r(sock) FD_CLR(sock, &readfds)
#define unmonitor_socket_w(sock) FD_CLR(sock, &writefds)



/* ===[ Client handlers ]==================================================== */

void cancel_request(struct client_node*);
void get_username(struct client_node*);
void idle_free(struct client_node*);
void idle_play(struct client_node*);
void send_data(struct client_node*);
void get_play_resp(struct client_node*);



/* ===[ Helpers ]============================================================ */

void accept_connection(void);
void client_disconnected(struct client_node*);
void inactive(struct client_node*);
void prepare_byte(struct client_node *client, uint8_t byte);
void prepare_client_list(struct client_node*);
void prepare_client_contact(struct client_node *to, struct client_node *contact);
void prepare_play_request(struct client_node *from, struct client_node *to);
void server_shell(void);
void start_match(struct client_node*, struct client_node*);



/* ===[ Data ]=============================================================== */

char buffer[BUFFER_SIZE];

fd_set readfds, writefds;
int maxfds = -1;

int sock_listen;

struct log_file *console;



/* ===[ Main ]=============================================================== */

int main (int argc, char **argv) {
	fd_set _readfds, _writefds;
	struct sockaddr_in myhost;
	int y = 1, s, i;
	
	/* Set log files */
	console = new_log(stdout, LOG_CONSOLE | LOG_INFO | LOG_ERROR, FALSE);
	open_log("tris_server.log", LOG_ALL);
	
	if ( argc != 3  ) {
		flog_message(LOG_CONSOLE, "Usage: %s <host> <listening_port>", argv[0]);
		exit(EXIT_FAILURE);
	}
	
	if ( inet_pton(AF_INET, argv[1], &(myhost.sin_addr)) != 1 ) {
		log_message(LOG_CONSOLE, "Invalid host address");
		exit(EXIT_FAILURE);
	}
	
	myhost.sin_family = AF_INET;
	myhost.sin_port = htons((uint16_t) atoi(argv[2])); /*FIXME check cast */
	memset(myhost.sin_zero, 0, sizeof(myhost.sin_zero));
	
	if ( (sock_listen = socket(myhost.sin_family, SOCK_STREAM, 0)) == -1 ) {
		log_error("Error socket()");
		exit(EXIT_FAILURE);
	}
	
	if ( setsockopt(sock_listen, SOL_SOCKET, SO_REUSEADDR, &y, sizeof(int)) ) {
		log_error("Error setsockopt()");
		exit(EXIT_FAILURE);
	}
	
	if ( bind(sock_listen, (struct sockaddr*) &myhost, sizeof(myhost)) ) {
		log_error("Error bind()");
		exit(EXIT_FAILURE);
	}
	
	if ( listen(sock_listen, BACKLOG) ) {
		log_error("Error listen()");
		exit(EXIT_FAILURE);
	}
	
	
	inet_ntop(AF_INET, &(myhost.sin_addr), buffer, INET_ADDRSTRLEN);
	flog_message(LOG_INFO, "Server listening on %s:%hu", buffer,
                                                        ntohs(myhost.sin_port));
	
	FD_ZERO(&readfds);
	FD_ZERO(&writefds);
	monitor_socket_r(sock_listen);
	monitor_socket_r(STDIN_FILENO);
	_readfds = readfds;
	_writefds = writefds;
	
	console->prompt = '>';
	log_prompt(console);
	
	while ( (s = select(maxfds + 1, &_readfds, &_writefds, NULL, NULL)) > 0 ) {
		for ( i = 0; i <= maxfds; i++ ) {
			if ( FD_ISSET(i, &_readfds) ) {
				
				if ( i == sock_listen ) {
					accept_connection();
				} else if ( i == STDIN_FILENO ) {
					server_shell();
				} else {
					struct client_node *client;
					int sock_client = i;
					
					client = get_client_by_socket(sock_client);
					if ( client != NULL && client->read_dispatch != NULL ) {
						if ( client->muted ) inactive(client);
						else client->read_dispatch(client);
					} else { /*FIXME non dovrebbe mai succedere */
						flog_message(LOG_WARNING,
          "Unexpected event on line %d, got read event on socket descriptor %d",
                                                                   __LINE__, i);
						
						unmonitor_socket_r(sock_client);
						unmonitor_socket_w(sock_client);
						shutdown(sock_client, SHUT_RDWR);
						close(sock_client);
					}
				}
				break; /* goto select() */
				
			} else if ( FD_ISSET(i, &_writefds) ) {
				
				struct client_node *client;
				int sock_client = i;
				
				client = get_client_by_socket(sock_client);
				if ( client != NULL && client->write_dispatch != NULL ) {
					client->write_dispatch(client);
				} else { /*FIXME non dovrebbe mai succedere */
					flog_message(LOG_WARNING,
         "Unexpected event on line %d, got write event on socket descriptor %d",
                                                                   __LINE__, i);
					
					unmonitor_socket_r(sock_client);
					unmonitor_socket_w(sock_client);
					shutdown(sock_client, SHUT_RDWR);
					close(sock_client);
				}
				break; /* goto select() */
				
			}
		}
		_readfds = readfds;
		_writefds = writefds;
	}
	
	flog_message(LOG_DEBUG, "Just exited main while loop with s=%d", s);
	
	for ( i = 0; i <= maxfds; i++ ) {
		if ( i != STDIN_FILENO &&
                         ( FD_ISSET(i, &readfds) || FD_ISSET(i, &writefds) ) ) {
			
			shutdown(i, SHUT_RDWR);
			close(i);
		}
	}
	
	log_error("Error select()");
	exit(EXIT_FAILURE);
}

/* ========================================================================== */

void accept_connection() {
	int sock_client;
	struct sockaddr_in clienthost;
	struct client_node *client;
	socklen_t addrlen = sizeof(clienthost);
	
	log_message(LOG_DEBUG, "Going to accept a new connection...");
	sock_client = accept(sock_listen, (struct sockaddr*) &clienthost, &addrlen);
	
	if ( sock_client < 0 ) {
		/*FIXME Potrebbe verificarsi ECONNABORTED, vorrei ritentare */
		log_error("Error accept()");
		close(sock_listen);
		exit(EXIT_FAILURE);
	}
	
	client = create_client_node();
	add_client_node(client);
	client->addr = clienthost;
	client->socket = sock_client;
	client->state = CONNECTED;
	client->read_dispatch = &get_username;
	flog_message(LOG_INFO, "Incoming connection from %s",
                                                     client_sockaddr_p(client));
	
	log_statechange(client);
	monitor_socket_r(sock_client);
}

void get_username(struct client_node *client) {
	uint8_t cmd;
	int received;
	struct client_node *dbl;
	
	received = recv(client->socket, &cmd, 1, 0);
	if ( received != 1 ) {
		flog_message(LOG_DEBUG, "Received=%d on line %d from %s", received,
                                              __LINE__, client_canon_p(client));
		
		client_disconnected(client);
		return;
	}
	
	if ( cmd != REQ_LOGIN ) {
		flog_message(LOG_WARNING, "Got BADREQ on line %d, cmd=%s from %s",
                             __LINE__, magic_name(cmd), client_canon_p(client));
		
		if ( cmd != RESP_BADREQ ) prepare_byte(client, RESP_BADREQ);
		return;
	}
	
	flog_message(LOG_DEBUG, "Got REQ_LOGIN from %s", client_sockaddr_p(client));
	
	received = recv(client->socket, &(client->username_len), 1, 0);
	
	if ( received != 1 ) {
		flog_message(LOG_WARNING, "Received=%d on line %d from %s", received,
                                              __LINE__, client_canon_p(client));
		
		client_disconnected(client);
		return;
	}
	
	if ( client->username_len < MIN_USERNAME_LENGTH || 
         client->username_len > MAX_USERNAME_LENGTH ) {
		flog_message(LOG_INFO_VERBOSE,
                            "Client %s tried to login with an invalid username",
                                                     client_sockaddr_p(client));
		
		prepare_byte(client, RESP_BADUSR);
		return;
	}
	
	received = recv(client->socket, buffer, client->username_len + 2, 0);
	
	if ( received != client->username_len + 2 ) {
		flog_message(LOG_WARNING, "Received=%d on line %d from %s",
                                    received, __LINE__, client_canon_p(client));
		
		client_disconnected(client);
		return;
	}
		
	unpack(buffer, "sw", client->username_len, &(client->username),
                                                           &(client->udp_port));
	
	if ( !username_is_valid(client->username, client->username_len) ) {
		/*TODO print escaped username string */
		flog_message(LOG_INFO_VERBOSE,
                               "Client %s tried to login with invalid username",
                                                     client_sockaddr_p(client));
		
		prepare_byte(client, RESP_BADUSR);
		return;	
	} 
	
	dbl = get_client_by_username(client->username);
	if ( dbl != NULL ) {
		flog_message(LOG_INFO_VERBOSE,
                           "Client %s tried to login with existing username=%s",
                                      client_sockaddr_p(client), dbl->username);
		
		prepare_byte(client, RESP_EXIST);
		return;
	}
	
	inet_ntop(AF_INET, &(client->addr.sin_addr), buffer, INET_ADDRSTRLEN);
	
	flog_message(LOG_INFO, "Client %s has username [%s]",
                                   client_sockaddr_p(client), client->username);
	
	flog_message(LOG_INFO, "[%s] Listening on %s:%hu (udp)",
                                    client->username, buffer, client->udp_port);
	
	client->state = FREE;
	/* client->read_dispatch = &idle_free; */
	log_statechange(client);
	prepare_byte(client, RESP_OK_LOGIN);
}

void idle_free(struct client_node *client) {
	uint8_t cmd, length;
	int received;
	struct client_node *opp;
	
	received = recv(client->socket, &cmd, 1, 0);
	if ( received != 1 ) {
		flog_message(LOG_DEBUG, "Received=%d on line %d from %s", received,
                                              __LINE__, client_canon_p(client));
		
		client_disconnected(client);
		return;
	}
	
	flog_message(LOG_DEBUG, "Got cmd=%s from %s in idle_free", magic_name(cmd),
                                                        client_canon_p(client));
	
	switch ( cmd ) {
		case REQ_WHO:
			prepare_client_list(client);
			break;
		
		case REQ_PLAY:
			received = recv(client->socket, &length, 1, 0);
			if ( received != 1 ) {
				flog_message(LOG_WARNING, "Received=%d on line %d from %s",
                                    received, __LINE__, client_canon_p(client));
				
				client_disconnected(client);
				return;
			}
			
			if ( length < MIN_USERNAME_LENGTH ||
				 length > MAX_USERNAME_LENGTH ) {
				flog_message(LOG_WARNING,
                    "[%s] requested to play with nonexistent player (too long)",
                                                              client->username);
				
				prepare_byte(client, RESP_NONEXIST);
				return;
			}
				
			received = recv(client->socket, buffer, length, 0);
			if ( received != length ) {
				flog_message(LOG_WARNING, "Received=%d on line %d from %s",
                                    received, __LINE__, client_canon_p(client));
				
				client_disconnected(client);
				return;
			}
					
			buffer[length] = '\0';
			opp = get_client_by_username(buffer);
			if ( opp == NULL ) {
				flog_message(LOG_INFO_VERBOSE,
                               "[%s] requested to play with nonexistent player",
                                                              client->username);
				
				prepare_byte(client, RESP_NONEXIST);
			} else if ( opp == client ) {
				flog_message(LOG_INFO_VERBOSE,
                       "[%s] requested to play with himself", client->username);
				
				prepare_byte(client, RESP_NONEXIST);
			} else if ( opp->state != FREE ) {
				flog_message(LOG_INFO_VERBOSE,
                               "[%s] requested to play with non-FREE player %s",
                                         client->username, client_canon_p(opp));
				
				prepare_byte(client, RESP_BUSY);
			} else prepare_play_request(client, opp);
			break;
		
		default:
			flog_message(LOG_WARNING, "Unexpected request from %s in idle_free",
                                                        client_canon_p(client));
			
			if ( cmd != RESP_BADREQ ) prepare_byte(client, RESP_BADREQ);
	}
}

void idle_play(struct client_node *client) {
	uint8_t cmd;
	int received;
	
	received = recv(client->socket, &cmd, 1, 0);
	if ( received != 1 ) {
		flog_message(LOG_DEBUG, "Received=%d on line %d from %s", received,
                                              __LINE__, client_canon_p(client));
		
		client_disconnected(client);
		return;
	}
	
	flog_message(LOG_DEBUG, "Got cmd=%s from %s in idle_play", magic_name(cmd),
                                                        client_canon_p(client));
	
	switch ( cmd ) {
		case REQ_WHO:
			prepare_client_list(client);
			break;
		
		case REQ_END:
		/*TODO create end_match() */
			if ( client->play_with != NULL ) {
				struct client_node *opp = client->play_with;
				flog_message(LOG_INFO, "[%s] stopped playing with [%s]",
                                               client->username, opp->username);
				
				if ( opp->state == ZOMBIE ) free(opp);
				else opp->play_with = get_zombie(client);
				client->play_with = NULL;
			} else if ( client->req_to != NULL ) {
				/*FIXME Events:
					- client send REQ_PLAY to opp
					- server send REQ_PLAY to opp
					- opp send RESP_OK_PLAY to server (client becomes PLAY but
					  play_with is significant, not req_to!!
					- client send REQ_END to server (cancel request) */
				/* cancel_request() */
				struct client_node *opp = client->req_to;
				flog_message(LOG_INFO, "[%s] cancelled playing with [%s]",
                                               client->username, opp->username);
				
				if ( opp->state == ZOMBIE ) free(opp);
				else opp->req_from = get_zombie(client);
				client->req_to = NULL;
			} else
				flog_message(LOG_INFO,
                              "[%s] stopped playing with a disconnected client",
                                                              client->username);
			
			client->state = FREE;
			log_statechange(client);
			prepare_byte(client, RESP_OK_FREE);
			break;
		
		default:
			flog_message(LOG_WARNING, "Unexpected request from %s in idle_play",
                                                        client_canon_p(client));
			
			if ( cmd != RESP_BADREQ ) prepare_byte(client, RESP_BADREQ);
	}
}

void cancel_request(struct client_node *client) {
	uint8_t cmd;
	int received;
	
	received = recv(client->socket, &cmd, 1, 0);
	if ( received != 1 ) {
		flog_message(LOG_DEBUG, "Received=%d on line %d from %s", received,
                                              __LINE__, client_canon_p(client));
		
		client_disconnected(client);
		return;
	}
	
	flog_message(LOG_DEBUG, "Got cmd=%s from %s in cancel_request",
                                       magic_name(cmd), client_canon_p(client));
	
	if ( cmd == REQ_END ) {
		if ( client->req_to != NULL ) {
			struct client_node *opp = client->req_to;
			flog_message(LOG_INFO, "[%s] cancelled playing with [%s]",
                                               client->username, opp->username);
			
			if ( opp->state == ZOMBIE ) free(opp);
			else opp->req_from = get_zombie(client);
			client->req_to = NULL;
		}
		client->state = FREE;
		log_statechange(client);
		prepare_byte(client, RESP_OK_FREE);
	} else if ( cmd != RESP_BADREQ )
		prepare_byte(client, RESP_BADREQ);
}

void client_disconnected(struct client_node *client) {
	struct client_node *opp;
	
	log_message(LOG_DEBUG, "Going to drop a client...");
	if ( client->state == BUSY ) {
		if ( client->req_from != NULL ) {
			opp = client->req_from;
			flog_message(LOG_INFO_VERBOSE, "[%s] had a play request from [%s]",
                                               client->username, opp->username);
			
			if ( opp->state == ZOMBIE ) free(opp);
			else {
				prepare_byte(opp, RESP_NONEXIST);
				opp->req_to = get_zombie(client);
				opp->state = FREE;
				log_statechange(opp);
			}
		} else if ( client->req_to != NULL ) {
			opp = client->req_to;
			flog_message(LOG_INFO_VERBOSE,
                       "[%s] had requested to play with [%s]", client->username,
                                                                 opp->username);
			
			/* opp->state == BUSY || opp->state == PLAY */
			if ( opp->state == ZOMBIE ) free(opp);
			else {
				opp->req_from = get_zombie(client);
				unmonitor_socket_w(opp->socket);
				monitor_socket_r(opp->socket); /*FIXME inutile? */
			}
		} else {
			/* in case client->req_from or req_to are set to NULL by
			cancel_request() or idle_play() */
			/*TODO use a better log message */
			/*FIXME should never be executed */
			flog_message(LOG_INFO_VERBOSE,
                           "[%s] had a play request with a disconnected client",
                                              client_canon_p(client), __LINE__);
		}
	} else if ( client->state == PLAY ) {
		opp = client->play_with;
		if ( opp != NULL ) {
			flog_message(LOG_INFO_VERBOSE, "[%s] was playing with [%s]",
                                               client->username, opp->username);
			
			if ( opp->state == ZOMBIE ) free(opp);
			else opp->play_with = get_zombie(client);
		} else {
			/*FIXME should never be executed */
			flog_message(LOG_INFO_VERBOSE, "[%s] was playing with [[unknown]]",
                                                              client->username);
		}
	}
	
	if ( client->state != NONE && client->state != CONNECTED )
		flog_message(LOG_INFO, "[%s] disconnected", client->username);
	else
		flog_message(LOG_INFO, "[[unknown]] %s disconnected",
                                                     client_sockaddr_p(client));
	
	unmonitor_socket_r(client->socket);
	unmonitor_socket_w(client->socket);
	shutdown(client->socket, SHUT_RDWR);
	close(client->socket);
	remove_client_node(client);
	destroy_client_node(client);
}

void prepare_byte(struct client_node *client, uint8_t byte) {
	flog_message(LOG_DEBUG, "Preparing to send %s to %s", magic_name(byte),
                                                        client_canon_p(client));
	
	client->byte_resp = byte;
	client->data = NULL;
	client->data_count = 1;
	client->data_cursor = 0;
	client->write_dispatch = &send_data;
	/* client->read_dispatch = &inactive; */
	client->muted = TRUE;
	monitor_socket_w(client->socket);
}

void send_data(struct client_node *client) {
	int sent;
	
	if (client->data == NULL)
		sent = send(client->socket, &(client->byte_resp), 1, 0);
	else
		sent = send(client->socket, client->data + client->data_cursor,
                                   client->data_count - client->data_cursor, 0);
	
	if ( sent < 0 ) {
		log_error("Error send()");
		client_disconnected(client);
		return;
	}
	
	client->data_cursor += sent;
	if ( client->data_cursor == client->data_count ) {
		flog_message(LOG_DEBUG, "Finished sending %d bytes of data to %s",
                                    client->data_count, client_canon_p(client));
		
		if ( client->data != NULL ) {
			flog_message(LOG_DEBUG, "Freeing %d bytes on line %d",
                                                  client->data_count, __LINE__);
			
			free(client->data);
			client->data = NULL;
		}
		
		flog_message(LOG_DEBUG, "%s is %s", client_canon_p(client),
                                                     state_name(client->state));
		
		switch ( client->state ) {
			case CONNECTED: client->read_dispatch = &get_username; break;
			case FREE: client->read_dispatch = &idle_free; break;
			case BUSY: client->read_dispatch = &get_play_resp; break;
			case PLAY: client->read_dispatch = &idle_play; break;
			default:
				flog_message(LOG_WARNING, "%s is %s on line %d",
                   client_canon_p(client), state_name(client->state), __LINE__);
				
				client_disconnected(client);
				return;
		}
		
		client->muted = FALSE;
		unmonitor_socket_w(client->socket);
	}
}

void server_shell() {
	int line_length;
	
	line_length = get_line(buffer, BUFFER_SIZE);
	
	log_message(LOG_USERINPUT, buffer);
	
	if ( strcmp(buffer, "help" ) == 0 ) { /* ------------------------- > help */
	
		log_message(LOG_CONSOLE, "Commands: help, who, playing, exit");
		
	} else if ( strcmp(buffer, "who") == 0 ) { /* --------------------- > who */
		
		struct client_node *cn;
		
		if (client_list.count == 0) {
			log_message(LOG_CONSOLE, "There are no connected clients.");
		} else {
			console->prompt = FALSE;
			flog_message(LOG_CONSOLE, "There are %d connected clients:",
                                                             client_list.count);
			
			for ( cn = client_list.head; cn != NULL; cn = cn->next ) {
				if ( cn->state == NONE || cn->state == CONNECTED )
					flog_message(LOG_CONSOLE,
                                           "[[unknown]] Host %s, not logged in",
                                                         client_sockaddr_p(cn));
				
				else
					flog_message(LOG_CONSOLE, "[%s] Host %s listening on %hu",
                             cn->username, client_sockaddr_p(cn), cn->udp_port);
			}
			console->prompt = '>';
			log_prompt(console);
		}
		
	} else if ( strcmp(buffer, "playing") == 0 ) { /* ------------- > playing */
		
		struct client_node *cn, *opp;
		bool playing = FALSE;
		
		console->prompt = FALSE;
		for ( cn = client_list.head; cn != NULL; cn = cn->next ) {
			if ( cn->state == PLAY && cn->play_with != NULL ) {
				opp = cn->play_with;
				if ( opp->state == PLAY && opp->play_with == cn ) {
					playing = TRUE;
					if ( strcmp(cn->username, opp->username) < 0 )
						flog_message(LOG_CONSOLE, "[%s] is playing with [%s]",
                                                   cn->username, opp->username);
				}
			}
		}
		if ( !playing ) log_message(LOG_CONSOLE, "No one is playing");
		console->prompt = '>';
		log_prompt(console);
		
	} else if ( strcmp(buffer, "exit") == 0 ) { /* ------------------- > exit */
		
		int i;
		flog_message(LOG_INFO_VERBOSE, "Closing %d client connections...",
                                                             client_list.count);
		
		for ( i = 0; i <= maxfds; i++ ) {
			if ( i == STDIN_FILENO ) continue;
			if ( FD_ISSET(i, &readfds) || FD_ISSET(i, &writefds) ) {
				shutdown(i, SHUT_RDWR);
				close(i);
			}
		}
		log_message(LOG_DEBUG, "Freeing data structures");
		destroy_client_list(client_list.head);
		log_message(LOG_CONSOLE, "Exiting...");
		exit(EXIT_SUCCESS);
		
	} else if ( strcmp(buffer, "") == 0 ) { /* ---------------------------- > */
		
		log_prompt(console);
		
	} else {
		
		log_message(LOG_CONSOLE, "Unknown command");
		
	}
}

void prepare_client_list(struct client_node *client) {
	uint32_t count = 0;
	int total_length = 1 + 4;
	struct client_node *cn;
	
	flog_message(LOG_INFO_VERBOSE,
              "[%s] requested the list of connected clients", client->username);
	
	for (cn = client_list.head; cn != NULL; cn = cn->next ) {
		if ( cn->state != NONE && cn->state != CONNECTED ) {
			count++;
			total_length += 1 + cn->username_len;
		}
	}
	
	flog_message(LOG_DEBUG, "Allocating %d bytes on line %d", total_length,
                                                                      __LINE__);
	
	client->data = malloc(total_length);
	check_alloc(client->data);
	client->data_cursor = 0;
	pack(client->data, "bl", RESP_WHO, count);
	client->data_count = 5;
	for (cn = client_list.head; cn != NULL; cn = cn->next ) {
		if ( cn->state != NONE && cn->state != CONNECTED ) {
			pack(client->data + client->data_count, "bs", cn->username_len,
                                                                  cn->username);
			
			client->data_count += 1 + cn->username_len;
		}
	}
	flog_message(LOG_DEBUG, "Preparing to send RESP_WHO data to [%s]",
                                                              client->username);
	
	client->write_dispatch = &send_data;
	monitor_socket_w(client->socket);
	/* client->read_dispatch = &inactive; */
	client->muted = TRUE;
}

void prepare_client_contact(struct client_node *to, struct client_node *cntc) {
	to->data_count = 1 + sizeof(cntc->addr.sin_addr) + sizeof(cntc->udp_port);
	flog_message(LOG_DEBUG, "Allocating %d bytes on line %d", to->data_count,
                                                                      __LINE__);
	
	to->data = malloc(to->data_count);
	check_alloc(to->data);
	pack(to->data, "blw", RESP_OK_PLAY, cntc->addr.sin_addr, cntc->udp_port);
	to->data_cursor = 0;
	flog_message(LOG_DEBUG, "Preparing to send RESP_OK_PLAY data to [%s]",
                                                                  to->username);
	
	to->write_dispatch = &send_data;
	monitor_socket_w(to->socket);
	to->muted = TRUE;
}

void prepare_play_request(struct client_node *from, struct client_node *to) {
	flog_message(LOG_INFO, "[%s] requested to play with [%s]", from->username,
                                                                  to->username);
	
	from->req_to = to;
	to->req_from = from;
	from->state = to->state = BUSY;
	log_statechange(from);
	log_statechange(to);
	to->data_count = 2 + from->username_len;
	flog_message(LOG_DEBUG, "Allocating %d bytes on line %d", to->data_count,
                                                                      __LINE__);
	
	to->data = malloc(to->data_count);
	check_alloc(to->data);
	pack(to->data, "bbs", REQ_PLAY, from->username_len, from->username);
	to->data_cursor = 0;
	flog_message(LOG_DEBUG, "Preparing to send REQ_PLAY data to [%s]",
                                                                  to->username);
	
	to->write_dispatch = &send_data;
	monitor_socket_w(to->socket);
	to->muted = TRUE;
	from->read_dispatch = &cancel_request;
}

void get_play_resp(struct client_node *client) {
	uint8_t resp;
	int received;
	struct client_node *opp = client->req_from;
	
	received = recv(client->socket, &resp, 1, 0);
	if ( received != 1 ) {
		flog_message(LOG_DEBUG, "Received=%d on line %d from %s", received,
                                              __LINE__, client_canon_p(client));
		
		client_disconnected(client);
		return;
	}
	
	if ( resp == RESP_OK_PLAY ) {
		if ( opp == NULL ) {
			/* opp si è disconnesso prima che client rispondesse. Facciamo
			comunque finta che sia ancora collegato, perché non c'è modo di
			comunicarlo a client */
			flog_message(LOG_INFO_VERBOSE,
                             "[%s] accepted to play with a disconnected client",
                                                              client->username);
			/*TODO oppure opp ha inviato REQ_END prima della play response */
		} else {
			flog_message(LOG_INFO, "[%s] accepted to play with [%s]",
                                               client->username, opp->username);
			
			if ( opp->state != ZOMBIE ) prepare_client_contact(opp, client);
		}
		
		start_match(client, opp);
	} else {
		/* FIXME May be some other valid command such as REQ_WHO or REQ_PLAY */
		/* RESP_REFUSE o, per sbaglio, anche RESP_BUSY / PERCHÉ?! */
		if ( resp != RESP_REFUSE )
			flog_message(LOG_WARNING, "Got %s in get_play_resp from %s",
                                      magic_name(resp), client_canon_p(client));
		
		if ( opp == NULL ) {
			flog_message(LOG_INFO_VERBOSE,
                              "[%s] refused to play with a disconnected client",
                                                              client->username);
			/*TODO oppure opp ha inviato REQ_END prima della play response */
		} else {
			flog_message(LOG_INFO, "[%s] refused to play with [%s]",
                                               client->username, opp->username);
			
			if ( opp->state == ZOMBIE ) free(opp);
			else {
				prepare_byte(opp, RESP_REFUSE);
				opp->state = FREE;
				opp->req_to = NULL;
				log_statechange(opp);
			}
		}
		client->state = FREE;
		client->req_from = NULL;
		client->read_dispatch = &idle_free;
		log_statechange(client);
	}
}

void start_match(struct client_node *a, struct client_node *b) {
	/* Symmetric */
	if ( a != NULL && a->state != ZOMBIE ) {
		a->state = PLAY;
		a->play_with = b;
		a->req_from = a->req_to = NULL;
		a->read_dispatch = &idle_play;
		log_statechange(a);
	}
	if ( b != NULL && b->state != ZOMBIE ) {
		b->state = PLAY;
		b->play_with = a;
		b->req_from = b->req_to = NULL;
		b->read_dispatch = &idle_play;
		log_statechange(b);
	}
	
	if ( a != NULL && b != NULL )
		flog_message(LOG_INFO, "[%s] is playing with [%s]", a->username,
                                                                   b->username);
	
	else if ( a != NULL )
		flog_message(LOG_INFO_VERBOSE,
                     "[%s] is playing with a disconnected client", a->username);
	
	else if ( b != NULL )
		flog_message(LOG_INFO_VERBOSE,
                     "[%s] is playing with a disconnected client", b->username);
	
	else
		flog_message(LOG_WARNING,
                "Unexpected event on line %d, both clients are NULL", __LINE__);
}

void inactive(struct client_node *client) {
	int received = recv(client->socket, buffer, BUFFER_SIZE, 0);
	
	if ( received == 0 )
		client_disconnected(client);
	
	else if ( received == 1 )
		flog_message(LOG_WARNING, "Got %s in inactive from %s",
                                 magic_name(buffer[0]), client_canon_p(client));
	
	else
		flog_message(LOG_WARNING, "Got %d bytes in inactive from %s", received,
                                                        client_canon_p(client));
	/*FIXME what to do? */
}
