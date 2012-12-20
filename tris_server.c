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

#define BACKLOG 5

#define update_maxfds(n) maxfds = (maxfds < (n) ? (n) : maxfds)
#define monitor_socket_r(sock) { FD_SET(sock, &readfds); update_maxfds(sock); }
#define monitor_socket_w(sock) { FD_SET(sock, &writefds); update_maxfds(sock); }
#define unmonitor_socket_r(sock) FD_CLR(sock, &readfds)
#define unmonitor_socket_w(sock) FD_CLR(sock, &writefds)

/* === Client handlers ====================================================== */

void get_username(struct client_node*);
void idle_free(struct client_node*);
void idle_play(struct client_node*);
void send_data(struct client_node*);
void get_play_resp(struct client_node*);
void inactive(struct client_node*);

/* === Helpers ============================================================== */

void accept_connection(void);
void client_disconnected(struct client_node*);
void start_match(struct client_node*);
void send_byte(struct client_node* client, uint8_t byte);
void server_shell(void);

/* === Data ================================================================= */

char buffer[BUFFER_SIZE];

fd_set readfds, writefds;
int maxfds = -1;
struct timeval tv = DEFAULT_TIMEOUT_INIT;

struct sockaddr_in myhost, yourhost;
int sock_listen, sock_client;
socklen_t addrlen = sizeof(yourhost);

int yes = 1, sel_status, i, received;

/* ========================================================================== */

int main (int argc, char **argv) {
	
	fd_set _readfds, _writefds;
	client_list.head = client_list.tail = NULL;
	client_list.count = 0;
	
	if ( argc != 3 /*|| strlen(argv[1]) < 7 || strlen(argv[1]) > 15 || strlen(argv[2]) > 5*/ ) {
		printf("Usage: %s <host> <porta>\n", argv[0]);
		return 1;
	}
	
	if ( inet_pton(AF_INET, argv[1], &(myhost.sin_addr)) != 1 ) {
		puts("Indirizzo host non valido");
		return 1;
	}
	
	myhost.sin_family = AF_INET;
	myhost.sin_port = htons((uint16_t) atoi(argv[2]));
	memset(myhost.sin_zero, 0, sizeof(myhost.sin_zero));
	
	if ( (sock_listen = socket(myhost.sin_family, SOCK_STREAM, 0)) == 1 ) {
		perror("Errore socket()");
		return 1;
	}
	
	if ( setsockopt(sock_listen, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) ) {
		perror("Errore setsockopt()");
		return 1;
	}
	
	if ( bind(sock_listen, (struct sockaddr*)&myhost, sizeof(myhost)) ) {
		perror("Errore bind()");
		return 1;
	}
	
	if ( listen(sock_listen, BACKLOG) ) {
		perror("Errore listen()");
		return 1;
	}
	
	inet_ntop(AF_INET, &(myhost.sin_addr), buffer, INET_ADDRSTRLEN);
	printf("Server listening on %s:%hu\n> ", buffer, ntohs(myhost.sin_port));
	fl();
	
	FD_ZERO(&readfds);
	FD_ZERO(&writefds);
	monitor_socket_r(sock_listen);
	monitor_socket_r(STDIN_FILENO);
	_readfds = readfds;
	_writefds = writefds;
	
	
	while ( (sel_status = select(maxfds + 1, &_readfds, &_writefds, NULL, &tv)) > 0 ) {
		for ( i = 0; i <= maxfds; i++ ) {
			if ( FD_ISSET(i, &_readfds) ) {
				if ( i == sock_listen ) {
					accept_connection();
				} else if ( i == STDIN_FILENO ) {
					server_shell();
				} else {
					struct client_node *client;
					sock_client = i;
					
					client = get_client_by_socket(sock_client);
					if ( client != NULL && client->read_dispatch != NULL ) {
						client->read_dispatch(client);
					} else { /* non dovrebbe mai succedere */
						unmonitor_socket_r(sock_client); /*FIXME */
						unmonitor_socket_w(sock_client);
						shutdown(sock_client, SHUT_RDWR);
						close(sock_client);
					}
				}
				break; /* goto select() */
				
			} else if ( FD_ISSET(i, &_writefds) ) {
				struct client_node *client;
				sock_client = i;
				
				client = get_client_by_socket(sock_client);
				if ( client != NULL && client->read_dispatch != NULL ) {
					client->write_dispatch(client);
				} else { /* non dovrebbe mai succedere */
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
	
	for ( i = 0; i <= maxfds; i++ ) {
		if ( i == STDIN_FILENO ) continue;
		if ( FD_ISSET(i, &readfds) || FD_ISSET(i, &writefds) ) {
			shutdown(i, SHUT_RDWR);
			close(i);
		}
	}
	
	if ( sel_status == 0 ) {
		puts("\nTimeout. Exit.");
		return 0;
	} else {
		perror("Errore su select()");
		return 1;
	}
}

void accept_connection() {
	if ( (sock_client = accept(sock_listen, (struct sockaddr*)&yourhost, &addrlen)) != -1 ) {
		struct client_node *client = create_client_node();
		inet_ntop(AF_INET, &(yourhost.sin_addr), buffer, INET_ADDRSTRLEN);
		printf("\nIncoming connection from %s:%hu\n> ", buffer, ntohs(yourhost.sin_port));
		fl();
		add_client_node(client);
		client->addr = yourhost;
		client->socket = sock_client;
		client->state = CONNECTED;
		client->read_dispatch = &get_username;
		monitor_socket_r(sock_client);
	} else {
		perror("Errore accept()");
		close(sock_listen);
		exit(1);
	}
}

void get_username(struct client_node *client) {
	uint8_t cmd;
	received = recv(client->socket, &cmd, 1, 0);
	
	if (received != 1) {
		client_disconnected(client);
		return;
	}
	
	if (cmd != REQ_LOGIN) {
		printf("\nBADREQ\n> "); fl();
		send_byte(client, RESP_BADREQ);
		return;
	}
		
	received = recv(client->socket, &(client->username_len), 1, 0); /*FIXME use select(_readfds) */
	if ( received == 1 ) {
		received = recv(client->socket, buffer, client->username_len + 2, 0);
		if ( received == client->username_len + 2 ) {
			unpack(buffer, "sw", client->username_len, &(client->username), &(client->udp_port));
			
			if ( !username_is_valid(client->username) ) 
				send_byte(client, RESP_BADUSR);
			else if ( get_client_by_username(client->username) != NULL )
				send_byte(client, RESP_EXIST);
			else {
				inet_ntop(AF_INET, &(client->addr.sin_addr), buffer, INET_ADDRSTRLEN);
				printf("\nClient %s:%hu has username [%s]\n", buffer, ntohs(client->addr.sin_port), client->username);
				printf("[%s] Listening on %s:%hu\n> ", client->username, buffer, client->udp_port);
				fl();
				
				client->state = FREE;
				client->read_dispatch = &idle_free;
				send_byte(client, RESP_OK_LOGIN);
			} 
		} else client_disconnected(client);
	} else client_disconnected(client);
}

void idle_free(struct client_node *client) {
	uint8_t cmd, length;
	int total_length;
	uint32_t count;
	struct client_node *cn;
	received = recv(client->socket, &cmd, 1, 0);
	if ( received != 1 ) {
		client_disconnected(client);
		return;
	}
	
	switch ( cmd ) {
		case REQ_WHO:
			count = 0;
			total_length = 1 + 4;
			for (cn = client_list.head; cn != NULL; cn = cn->next ) {
				if ( cn->state != NONE && cn->state != CONNECTED ) {
					count++;
					total_length += 1 + cn->username_len;
				}
			}
			
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
			client->write_dispatch = &send_data;
			monitor_socket_w(client->socket);
			client->read_dispatch = &inactive;
			break;
		
		case REQ_PLAY:
			received = recv(client->socket, &length, 1, 0);
			if ( received == 1 ) {
				received = recv(client->socket, buffer, length, 0);
				if ( received == length ) {
					struct client_node *opp;
					buffer[length] = '\0';
					opp = get_client_by_username(buffer);
					if ( opp == NULL || opp == client ) send_byte(client, RESP_NONEXIST);
					else if ( opp->state != FREE ) send_byte(client, RESP_BUSY);
					else {
						printf("\n[%s] requested to play with [%s]\n> ", client->username, opp->username);
						fl();
						client->req_to = opp;
						opp->req_from = client;
						client->state = opp->state = BUSY;
						opp->data_count = 2 + client->username_len;
						opp->data = malloc(opp->data_count);
						check_alloc(opp->data);
						pack(opp->data, "bbs", REQ_PLAY, client->username_len, client->username);
						opp->data_cursor = 0;
						opp->write_dispatch = &send_data;
						monitor_socket_w(opp->socket);
						opp->read_dispatch = &inactive;
						client->read_dispatch = &inactive;
					}
				} else client_disconnected(client);
			} else client_disconnected(client);
			break;
		
		/* see get_play_resp ( client->req_from == NULL ) */
		case RESP_REFUSE:
		case RESP_OK_PLAY:
		/* case REQ_END: */
			if ( client->state == BUSY ) {
				client->state = FREE;
				break;
			} /* else fallthrough */
		
		default:
			send_byte(client, RESP_BADREQ);
	}
}

void idle_play(struct client_node *client) {
	
}

void client_disconnected(struct client_node *client) {
	inet_ntop(AF_INET, &(client->addr.sin_addr), buffer, INET_ADDRSTRLEN);
	if ( client->state != NONE && client->state != CONNECTED )
		printf("\n[%s] %s:%hu disconnected\n> ", client->username, buffer, ntohs(client->addr.sin_port));
	else
		printf("\n[unknown] %s:%hu disconnected\n> ", buffer, ntohs(client->addr.sin_port));
	
	fl();
	if ( client->state == BUSY ) {
		struct client_node *opp;
		if ( client->req_from ) {
			opp = client->req_from;
			send_byte(opp, RESP_NONEXIST);
			opp->req_to = NULL;
			opp->state = FREE;
		} else if ( client->req_to ) {
			/*TODO */
			opp = client->req_to;
			/* opp->state = BUSY; */
			opp->req_from = NULL;
			opp->read_dispatch = &idle_free;
			unmonitor_socket_w(opp->socket);
			monitor_socket_r(opp->socket); /*FIXME inutile? */
		}

	}

	unmonitor_socket_r(client->socket);
	unmonitor_socket_w(client->socket);
	shutdown(client->socket, SHUT_RDWR);
	close(client->socket);
	remove_client_node(client);
	destroy_client_node(client);
}

void send_byte(struct client_node *client, uint8_t byte) {
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
		sent = send(client->socket, client->data, client->data_count - client->data_cursor, 0);
	
	if ( sent > 0 ) {
		client->data_cursor += sent;
		if ( client->data_cursor == client->data_count ) {
			if ( client->data != NULL ) free(client->data);
			client->data = NULL;
			
			switch ( client->state ) {
				case CONNECTED: client->read_dispatch = &get_username; break;
				case FREE: client->read_dispatch = &idle_free; break;
				case BUSY:
					if ( client->req_from ) client->read_dispatch = &get_play_resp;
					else if ( client->req_to ) start_match(client);
					break;
				case PLAY: client->read_dispatch = &idle_play; break;
				default: client_disconnected(client); return;
			}
			
			monitor_socket_r(client->socket);
			unmonitor_socket_w(client->socket);
		}
	} else client_disconnected(client);
}

void server_shell() {
	fgets(buffer, BUFFER_SIZE, stdin);
	
	if ( strcmp(buffer, "help\n" ) == 0 || strcmp(buffer, "?\n") == 0 ) {
		printf("Commands: help, who, playing, exit\n> ");
		fl();
	} else if ( strcmp(buffer, "who\n") == 0 ) {
		struct client_node *cn;
		if (client_list.count == 0) {
			printf("There are no connected clients.\n> ");
			fl();
		} else {
			printf("There are %d connected clients:\n", client_list.count);
			fl();
			for ( cn = client_list.head; cn != NULL; cn = cn->next ) {
				inet_ntop(AF_INET, &(cn->addr.sin_addr), buffer, INET_ADDRSTRLEN);
				if ( cn->state == NONE || cn->state == CONNECTED )
					printf("[unknown] Host %s:%hu, not logged in\n", buffer, ntohs(cn->addr.sin_port));
				else
					printf("[%s] Host %s:%hu listening on %hu\n", cn->username, buffer, ntohs(cn->addr.sin_port), cn->udp_port);
			}
			printf("> "); fl();
		}
	} else if ( strcmp(buffer, "playing\n") == 0 ) {
		/*TODO */
	} else if ( strcmp(buffer, "exit\n") == 0 ) {
		puts("Exiting...");
		for ( i = 0; i <= maxfds; i++ ) {
			if ( i == STDIN_FILENO ) continue;
			if ( FD_ISSET(i, &readfds) || FD_ISSET(i, &writefds) ) {
				shutdown(i, SHUT_RDWR);
				close(i);
			}
		}
		destroy_client_list(client_list.head);
		exit(0);
	} else if ( strcmp(buffer, "\n") == 0 ) {
		printf("> "); fl();
	} else {
		printf("Unknown command\n> ");
		fl();
	}
}

void get_play_resp(struct client_node *client) {
	uint8_t resp;
	received = recv(client->socket, &resp, 1, 0);
	if ( received == 1 ) {
		struct client_node *opp = client->req_from;
		if ( opp == NULL ) return; /* non dovrebbe mai succedere
			client->req_from viene messo a NULL solo se si disconnette PRIMA
			che arrivi la risposta da client e read_dispatch = &idle_free, che
			gestisce l'arrivo della risposta (fino a quel momento client->state
			rimane BUSY) */
		if ( resp == RESP_OK_PLAY ) {
			printf("\n[%s] accepted to play with [%s]\n> ", client->username, opp->username);
			fl();
			opp->data_count = 1 + sizeof(client->addr.sin_addr) + sizeof(client->udp_port);
			opp->data = malloc(opp->data_count);
			check_alloc(opp->data);
			pack(opp->data, "blw", RESP_OK_PLAY, client->addr.sin_addr, client->udp_port);
			opp->data_cursor = 0;
			opp->write_dispatch = &send_data;
			monitor_socket_w(opp->socket);
			client->read_dispatch = &inactive;
		} else { /*FIXME May be some other valid command such as REQ_WHO or REQ_PLAY */
			/* RESP_REFUSE o, per sbaglio, anche RESP_BUSY */
			printf("\n[%s] refused to play with [%s]\n> ", client->username, opp->username);
			fl();
			send_byte(opp, RESP_REFUSE);
			opp->state = FREE;
			opp->req_to = NULL;
			client->state = FREE;
			client->req_from = NULL;
			client->read_dispatch = &idle_free;
		}
	} else client_disconnected(client);
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
	monitor_socket_r(opp->socket);
}

void inactive(struct client_node *client) {
	uint8_t cmd;
	received = recv(client->socket, &cmd, 1, 0);
	if ( received == 0 ) client_disconnected(client);
	/*FIXME what to do? */
}
