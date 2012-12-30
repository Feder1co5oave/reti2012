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

void get_username(struct client_node*);
void idle_free(struct client_node*);
void idle_play(struct client_node*);
void send_data(struct client_node*);
void get_play_resp(struct client_node*);
void inactive(struct client_node*);



/* ===[ Helpers ]============================================================ */

void accept_connection(void);
void client_disconnected(struct client_node*);
void start_match(struct client_node*);
void send_byte(struct client_node* client, uint8_t byte);
void server_shell(void);



/* ===[ Data ]=============================================================== */

char buffer[BUFFER_SIZE];

fd_set readfds, writefds;
int maxfds = -1;

int sock_listen;

struct log_file *console;



/* ===[ Main ]=============================================================== */

int main (int argc, char **argv) {
	fd_set _readfds, _writefds;
	/* struct timeval tv = DEFAULT_TIMEOUT_INIT; */
	struct sockaddr_in myhost;
	int yes = 1, sel_status, i;

	/* Set log files */
	console = new_log(stdout, LOG_CONSOLE | LOG_INFO | LOG_ERROR, FALSE);
	open_log("tris_server.log", LOG_ALL);
	
	if ( argc != 3 /*|| strlen(argv[1]) < 7 || strlen(argv[1]) > 15 || strlen(argv[2]) > 5*/ ) {
		flog_message(LOG_CONSOLE, "Usage: %s <host> <porta>", argv[0]);
		return 1;
	}
	
	if ( inet_pton(AF_INET, argv[1], &(myhost.sin_addr)) != 1 ) {
		log_message(LOG_CONSOLE, "Invalid host address");
		return 1;
	}
	
	myhost.sin_family = AF_INET;
	myhost.sin_port = htons((uint16_t) atoi(argv[2])); /*FIXME check cast */
	memset(myhost.sin_zero, 0, sizeof(myhost.sin_zero));
	
	if ( (sock_listen = socket(myhost.sin_family, SOCK_STREAM, 0)) == 1 ) {
		log_error("Errore socket()");
		return 1;
	}
	
	if ( setsockopt(sock_listen, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) ) {
		log_error("Errore setsockopt()");
		return 1;
	}
	
	if ( bind(sock_listen, (struct sockaddr*) &myhost, sizeof(myhost)) ) {
		log_error("Errore bind()");
		return 1;
	}
	
	if ( listen(sock_listen, BACKLOG) ) {
		log_error("Errore listen()");
		return 1;
	}
	
	
	inet_ntop(AF_INET, &(myhost.sin_addr), buffer, INET_ADDRSTRLEN);
	flog_message(LOG_INFO, "Server listening on %s:%hu", buffer, ntohs(myhost.sin_port));
	
	FD_ZERO(&readfds);
	FD_ZERO(&writefds);
	monitor_socket_r(sock_listen);
	monitor_socket_r(STDIN_FILENO);
	_readfds = readfds;
	_writefds = writefds;
	
	console->prompt = '>';
	prompt(>);
	
	while ( (sel_status = select(maxfds + 1, &_readfds, &_writefds, NULL, &tv)) > 0 ) {
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
						client->read_dispatch(client);
					} else { /* non dovrebbe mai succedere */
						flog_message(LOG_WARNING, "Unexpected event on line %d, got read event on socket descriptor %d", __LINE__, i);
						unmonitor_socket_r(sock_client); /*FIXME */
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
				if ( client != NULL && client->read_dispatch != NULL ) {
					client->write_dispatch(client);
				} else { /* non dovrebbe mai succedere */
					flog_message(LOG_WARNING, "Unexpected event on line %d, got write event on socket descriptor %d", __LINE__, i);
					unmonitor_socket_r(sock_client); /*FIXME */
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

	flog_message(LOG_DEBUG, "Just exited main while loop with sel_status=%d", sel_status);
	
	for ( i = 0; i <= maxfds; i++ ) {
		if ( i == STDIN_FILENO ) continue;
		if ( FD_ISSET(i, &readfds) || FD_ISSET(i, &writefds) ) {
			shutdown(i, SHUT_RDWR);
			close(i);
		}
	}
	
	if ( sel_status == 0 ) {
		log_message(LOG_INFO, "Timeout. Exit.");
		return 0;
	} else {
		log_error("Errore su select()");
		return 1;
	}
}

/* ========================================================================== */

void accept_connection() { /*TODO rendere locali alcune variabili */
	int sock_client;
	struct sockaddr_in yourhost;
	socklen_t addrlen = sizeof(yourhost);

	log_message(LOG_DEBUG, "Going to accept a new connection...");
	if ( (sock_client = accept(sock_listen, (struct sockaddr*) &yourhost, &addrlen)) >= 0 ) {
		struct client_node *client = create_client_node();
		add_client_node(client);
		client->addr = yourhost;
		client->socket = sock_client;
		client->state = CONNECTED;
		client->read_dispatch = &get_username;
		flog_message(LOG_INFO, "Incoming connection from %s", client_sockaddr_p(client));
		log_statechange(client);
		monitor_socket_r(sock_client);
	} else {
		/*FIXME Potrebbe verificarsi ECONNABORTED e in quel caso vorrei ritentare */
		log_error("Errore accept()");
		close(sock_listen);
		exit(1);
	}
}

void get_username(struct client_node *client) {
	uint8_t cmd;
	int received;

	received = recv(client->socket, &cmd, 1, 0);
	if (received != 1) {
		flog_message(LOG_WARNING, "Received=%d on line %d from %s", received, __LINE__, client_canon_p(client));
		client_disconnected(client);
		return;
	}
	
	if (cmd != REQ_LOGIN) {
		flog_message(LOG_WARNING, "Got BADREQ on line %d, cmd=%s from %s", __LINE__, magic_name(cmd), client_canon_p(client));
		send_byte(client, RESP_BADREQ);
		return;
	}

	flog_message(LOG_DEBUG, "Got REQ_LOGIN from %s", client_sockaddr_p(client));
		
	received = recv(client->socket, &(client->username_len), 1, 0); /*FIXME use select(_readfds) */

	if ( received == 1 ) {
		if ( client->username_len > MAX_USERNAME_LENGTH ) {
			flog_message(LOG_INFO_VERBOSE, "Client %s tried to login with invalid username", client_sockaddr_p(client));
			send_byte(client, RESP_BADUSR);
			return;
		}

		received = recv(client->socket, buffer, client->username_len + 2, 0);
		if ( received == client->username_len + 2 ) {
			struct client_node *dbl;
			unpack(buffer, "sw", client->username_len, &(client->username), &(client->udp_port));
			if ( !username_is_valid(client->username) ) {
				/*TODO print escaped username string */
				flog_message(LOG_INFO_VERBOSE, "Client %s tried to login with invalid username", client_sockaddr_p(client));
				send_byte(client, RESP_BADUSR);
			} else if ( (dbl = get_client_by_username(client->username)) != NULL ) {
				flog_message(LOG_INFO_VERBOSE, "Client %s tried to login with existing username=%s", client_sockaddr_p(client), dbl->username);
				send_byte(client, RESP_EXIST);
			} else {
				inet_ntop(AF_INET, &(client->addr.sin_addr), buffer, INET_ADDRSTRLEN);
				flog_message(LOG_INFO, "Client %s has username [%s]", client_sockaddr_p(client), client->username);
				flog_message(LOG_INFO, "[%s] Listening on %s:%hu (udp)", client->username, buffer, client->udp_port);
				
				client->state = FREE;
				client->read_dispatch = &idle_free;
				log_statechange(client);
				send_byte(client, RESP_OK_LOGIN);
			}
		} else {
			flog_message(LOG_WARNING, "Received=%d on line %d from %s", received, __LINE__, client_canon_p(client));
			client_disconnected(client);
		}
	} else {
		flog_message(LOG_WARNING, "Received=%d on line %d from %s", received, __LINE__, client_canon_p(client));
		client_disconnected(client);
	}
}

void idle_free(struct client_node *client) {
	uint8_t cmd, length;
	int total_length, received;
	uint32_t count;
	struct client_node *cn;

	received = recv(client->socket, &cmd, 1, 0);
	if ( received != 1 ) {
		flog_message(LOG_WARNING, "Received=%d on line %d from %s", received, __LINE__, client_canon_p(client));
		client_disconnected(client);
		return;
	}
	
	flog_message(LOG_DEBUG, "Got cmd=%s from %s in idle_free", magic_name(cmd), client_canon_p(client));

	switch ( cmd ) {
		case REQ_WHO:
			flog_message(LOG_INFO_VERBOSE, "[%s] requested the list of connected clients", client->username);
			count = 0;
			total_length = 1 + 4;
			for (cn = client_list.head; cn != NULL; cn = cn->next ) {
				if ( cn->state != NONE && cn->state != CONNECTED ) {
					count++;
					total_length += 1 + cn->username_len;
				}
			}
			
			flog_message(LOG_DEBUG, "Allocating %d bytes on line %d", total_length, __LINE__);
			client->data = malloc(total_length);
			check_alloc(client->data);
			client->data_cursor = 0;
			pack(client->data, "bl", RESP_WHO, count);
			client->data_count = 5;
			for (cn = client_list.head; cn != NULL; cn = cn->next ) {
				if ( cn->state != NONE && cn->state != CONNECTED ) {
					pack(client->data + client->data_count, "bs", cn->username_len, cn->username);
					client->data_count += 1 + cn->username_len;
				}
			}
			flog_message(LOG_DEBUG, "Preparing to send RESP_WHO data to [%s]", client->username);
			client->write_dispatch = &send_data;
			monitor_socket_w(client->socket);
			client->read_dispatch = &inactive;
			break;
		
		case REQ_PLAY:
			received = recv(client->socket, &length, 1, 0);
			if ( received == 1 ) {
				if ( length > MAX_USERNAME_LENGTH ) {
					flog_message(LOG_WARNING, "[%s] requested to play with nonexistent player (too long)", client->username);
					send_byte(client, RESP_NONEXIST);
					break;
				}

				received = recv(client->socket, buffer, length, 0);
				if ( received == length ) {
					struct client_node *opp;
					buffer[length] = '\0';
					opp = get_client_by_username(buffer);
					if ( opp == NULL ) {
						flog_message(LOG_INFO_VERBOSE, "[%s] requested to play with nonexistent player", client->username);
						send_byte(client, RESP_NONEXIST);
					} else if ( opp == client ) {
						flog_message(LOG_INFO_VERBOSE, "[%s] requested to play with himself", client->username);
						send_byte(client, RESP_NONEXIST);
					} else if ( opp->state != FREE ) {
						flog_message(LOG_INFO_VERBOSE, "[%s] requested to play with non-FREE player %s", client->username, client_canon_p(opp));
						send_byte(client, RESP_BUSY);
					} else {
						flog_message(LOG_INFO, "[%s] requested to play with [%s]", client->username, opp->username);
						client->req_to = opp;
						opp->req_from = client;
						client->state = opp->state = BUSY;
						log_statechange(client);
						log_statechange(opp);
						opp->data_count = 2 + client->username_len;
						flog_message(LOG_DEBUG, "Allocating %d bytes on line %d", opp->data_count, __LINE__);
						opp->data = malloc(opp->data_count);
						check_alloc(opp->data);
						pack(opp->data, "bbs", REQ_PLAY, client->username_len, client->username);
						opp->data_cursor = 0;
						flog_message(LOG_DEBUG, "Preparing to send REQ_PLAY data to [%s]", opp->username);
						opp->write_dispatch = &send_data;
						monitor_socket_w(opp->socket);
						opp->read_dispatch = &inactive;
						client->read_dispatch = &inactive;
					}
				} else {
					flog_message(LOG_WARNING, "Received=%d on line %d from %s", received, __LINE__, client_canon_p(client));
					client_disconnected(client);
				}
			} else {
				flog_message(LOG_WARNING, "Received=%d on line %d from %s", received, __LINE__, client_canon_p(client));
				client_disconnected(client);
			}
			break;
		
		/* see get_play_resp ( client->req_from == NULL ) */
		case RESP_REFUSE:
		case RESP_OK_PLAY:
		/* case REQ_END: */
			if ( client->state == BUSY ) {
				client->state = FREE;
				log_statechange(client);
				break;
			} else {
				flog_message(LOG_WARNING, "%s is %s (should be BUSY)", client_canon_p(client), state_name(client->state));
				/* fallthrough */
			}
		
		default:
			flog_message(LOG_WARNING, "Unexpected request from %s in idle_free", client_canon_p(client));
			send_byte(client, RESP_BADREQ);
	}
}

void idle_play(struct client_node *client) {
	log_message(LOG_DEBUG, "idle_play not implemented yet");
	client_disconnected(client);
}

void client_disconnected(struct client_node *client) {
	log_message(LOG_DEBUG, "Going to drop a client...");
	if ( client->state == BUSY ) {
		struct client_node *opp;
		if ( client->req_from ) {
			opp = client->req_from;
			flog_message(LOG_INFO_VERBOSE, "[%s] had a play request from [%s]", client->username, opp->username);
			send_byte(opp, RESP_NONEXIST);
			opp->req_to = NULL;
			opp->state = FREE;
			log_statechange(opp);
		} else if ( client->req_to ) {
			/*TODO */
			opp = client->req_to;
			flog_message(LOG_INFO_VERBOSE, "[%s] had requested to play with [%s]", client->username, opp->username);
			/* opp->state = BUSY; */
			opp->req_from = NULL;
			opp->read_dispatch = &idle_free;
			unmonitor_socket_w(opp->socket);
			monitor_socket_r(opp->socket); /*FIXME inutile? */
		} else {
			flog_message(LOG_WARNING, "%s has unconsistent data on line %d", client_canon_p(client), __LINE__);
		}
	} /*TODO else if ( client->state == PLAY ) */

	if ( client->state != NONE && client->state != CONNECTED )
		flog_message(LOG_INFO, "[%s] %s disconnected", client->username, client_sockaddr_p(client));
	else
		flog_message(LOG_INFO, "[[unknown]] %s disconnected", client_sockaddr_p(client));

	unmonitor_socket_r(client->socket);
	unmonitor_socket_w(client->socket);
	shutdown(client->socket, SHUT_RDWR);
	close(client->socket);
	remove_client_node(client);
	destroy_client_node(client);
}

void send_byte(struct client_node *client, uint8_t byte) {
	flog_message(LOG_DEBUG, "Preparing to send %s to %s", magic_name(byte), client_canon_p(client));
	client->byte_resp = byte;
	client->data = NULL;
	client->data_count = 1;
	client->data_cursor = 0;
	client->write_dispatch = &send_data;
	client->read_dispatch = &inactive;
	monitor_socket_w(client->socket);
}

void send_data(struct client_node *client) {
	int sent;

	if (client->data == NULL)
		sent = send(client->socket, &(client->byte_resp), 1, 0);
	else
		sent = send(client->socket, client->data + client->data_cursor, client->data_count - client->data_cursor, 0);
	
	if ( sent > 0 ) {
		client->data_cursor += sent;
		if ( client->data_cursor == client->data_count ) {
			flog_message(LOG_DEBUG, "Finished sending %d bytes of data to %s", client->data_count, client_canon_p(client));
			if ( client->data != NULL ) {
				flog_message(LOG_DEBUG, "Freeing %d bytes on line %d", client->data_count, __LINE__);
				free(client->data);
				client->data = NULL;
			}
			
			flog_message(LOG_DEBUG, "%s is %s", client_canon_p(client), state_name(client->state));
			switch ( client->state ) {
				case CONNECTED: client->read_dispatch = &get_username; break;
				case FREE: client->read_dispatch = &idle_free; break;
				case BUSY:
					if ( client->req_from ) client->read_dispatch = &get_play_resp;
					else if ( client->req_to ) start_match(client);
					else flog_message(LOG_WARNING, "%s has unconsistent data on line %d", client_canon_p(client), __LINE__);
					break;
				case PLAY: client->read_dispatch = &idle_play; break;
				default:
					flog_message(LOG_WARNING, "%s has unconsistent data on line %d", client_canon_p(client), __LINE__);
					client_disconnected(client);
					return;
			}
			
			monitor_socket_r(client->socket);
			unmonitor_socket_w(client->socket);
		}
	} else {
		flog_message(LOG_WARNING, "Sent=%d on line %d to %s", sent, __LINE__, client_canon_p(client));
		client_disconnected(client);
	}
}

void server_shell() {
	int line_length;
	fgets(buffer, BUFFER_SIZE, stdin);
	line_length = strlen(buffer);
	if ( line_length > 0 ) buffer[line_length - 1] = '\0';
	
	log_message(LOG_USERINPUT, buffer);
	
	if ( strcmp(buffer, "help" ) == 0 || strcmp(buffer, "?") == 0 ) {
		log_message(LOG_CONSOLE, "Commands: help, who, playing, exit");
	} else if ( strcmp(buffer, "who") == 0 ) {
		struct client_node *cn;
		
		if (client_list.count == 0) {
			log_message(LOG_CONSOLE, "There are no connected clients.");
		} else {
			console->prompt = FALSE;
			flog_message(LOG_CONSOLE, "There are %d connected clients:", client_list.count);
			for ( cn = client_list.head; cn != NULL; cn = cn->next ) {
				if ( cn->state == NONE || cn->state == CONNECTED )
					flog_message(LOG_CONSOLE, "[[unknown]] Host %s, not logged in", client_sockaddr_p(cn));
				else
					flog_message(LOG_CONSOLE, "[%s] Host %s listening on %hu", cn->username, client_sockaddr_p(cn), cn->udp_port);
			}
			console->prompt = '>';
			prompt(>);
		}
	} else if ( strcmp(buffer, "playing") == 0 ) {
		struct client_node *cn, *opp;
		bool playing = FALSE;

		console->prompt = FALSE;
		for ( cn = client_list.head; cn != NULL; cn = cn->next ) {
			if ( cn->state == PLAY && cn->play_with != NULL ) {
				opp = cn->play_with;
				playing = TRUE;
				if ( strcmp(cn->username, opp->username) < 0 )
					flog_message(LOG_CONSOLE, "[%s] is playing with [%s]", cn->username, opp->username);
			}
		}
		if ( !playing ) log_message(LOG_CONSOLE, "No one is playing");
		console->prompt = '>';
		prompt(>);
	} else if ( strcmp(buffer, "exit") == 0 ) {
		int i;
		flog_message(LOG_INFO_VERBOSE, "Closing %d client connections...", client_list.count);
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
		exit(0);
	} else if ( strcmp(buffer, "") == 0 ) {
		prompt(>);
	} else {
		log_message(LOG_CONSOLE, "Unknown command");
	}
}

void get_play_resp(struct client_node *client) {
	uint8_t resp;
	int received;

	received = recv(client->socket, &resp, 1, 0);
	if ( received == 1 ) {
		struct client_node *opp = client->req_from;
		if ( opp == NULL ) { /* non dovrebbe mai succedere
			client->req_from viene messo a NULL solo se si disconnette PRIMA
			che arrivi la risposta da client e read_dispatch = &idle_free, che
			gestisce l'arrivo della risposta (fino a quel momento client->state
			rimane BUSY) */
			flog_message(LOG_WARNING, "Unexpected client state on line %d in get_play_resp from %s", client_canon_p(client));
			return;
		}
		if ( resp == RESP_OK_PLAY ) {
			flog_message(LOG_INFO, "[%s] accepted to play with [%s]", client->username, opp->username);
			opp->data_count = 1 + sizeof(client->addr.sin_addr) + sizeof(client->udp_port);
			flog_message(LOG_DEBUG, "Allocating %d bytes on line %d", opp->data_count, __LINE__);
			opp->data = malloc(opp->data_count);
			check_alloc(opp->data);
			pack(opp->data, "blw", RESP_OK_PLAY, client->addr.sin_addr, client->udp_port);
			opp->data_cursor = 0;
			flog_message(LOG_DEBUG, "Preparing to send RESP_OK_PLAY data to [%s]", opp->username);
			opp->write_dispatch = &send_data;
			monitor_socket_w(opp->socket);
			client->read_dispatch = &inactive;
		} else { /*FIXME May be some other valid command such as REQ_WHO or REQ_PLAY */
			/* RESP_REFUSE o, per sbaglio, anche RESP_BUSY */
			flog_message(LOG_INFO, "[%s] refused to play with [%s]", client->username, opp->username);
			send_byte(opp, RESP_REFUSE);
			opp->state = FREE;
			opp->req_to = NULL;
			client->state = FREE;
			client->req_from = NULL;
			client->read_dispatch = &idle_free;
			log_statechange(opp);
			log_statechange(client);
		}
	} else {
		flog_message(LOG_WARNING, "Received=%d on line %d from %s", received, __LINE__, client_canon_p(client));
		client_disconnected(client);
	}
}

void start_match(struct client_node *client) {
	struct client_node *opp = client->req_to;
	client->state = PLAY;
	client->play_with = opp;
	client->req_to = NULL;
	client->read_dispatch = &idle_play;
	opp->state = PLAY;
	opp->play_with = client;
	opp->req_from = NULL;
	opp->read_dispatch = &idle_play;
	log_statechange(client);
	log_statechange(opp);
	flog_message(LOG_INFO, "[%s] is playing with [%s]", client->username, opp->username);
	monitor_socket_r(opp->socket); /*FIXME inutile? */
}

void inactive(struct client_node *client) {
	uint8_t cmd;
	int received;

	received = recv(client->socket, &cmd, 1, 0);
	if ( received == 0 ) client_disconnected(client);
	else flog_message(LOG_WARNING, "Got %s in inactive from %s", magic_name(cmd), client_canon_p(client));
	/*FIXME what to do? */
}
