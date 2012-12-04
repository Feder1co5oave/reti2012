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
void client_disconnected(struct client_node*);
void idle_free(struct client_node*);
void idle_busy(struct client_node*);
void send_data(struct client_node*);
void send_byte(struct client_node* client, uint8_t byte);

/* ========================================================================== */

char buffer[4097];

fd_set readfds, writefds;
int maxfds = -1;
struct timeval tv = DEFAULT_TIMEOUT_INIT;

struct sockaddr_in myhost, yourhost;
int sock_listen, sock_client;
socklen_t addrlen = sizeof(yourhost);

int yes = 1, sel_status, i, received;

int main (int argc, char **argv) {
	
	fd_set _readfds, _writefds;
	client_list.head = client_list.tail = NULL;
	client_list.count = 0;
	
	if ( argc != 3 /*|| strlen(argv[1]) < 7 || strlen(argv[1]) > 15 || strlen(argv[2]) > 5*/ ) {
		puts("Usage: tris_server <host> <porta>");
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
	printf("Server listening on %s:%hu\n", buffer, ntohs(myhost.sin_port));
	
	FD_ZERO(&readfds);
	FD_ZERO(&writefds);
	monitor_socket_r(sock_listen);
	_readfds = readfds;
	_writefds = writefds;
	
	
	while ( (sel_status = select(maxfds + 1, &_readfds, &_writefds, NULL, &tv)) > 0 ) {
		for ( i = 0; i <= maxfds; i++ ) {
			if ( FD_ISSET(i, &_readfds) ) {
				if ( i == sock_listen ) {
					if ( (sock_client = accept(sock_listen, (struct sockaddr*)&yourhost, &addrlen)) != -1 ) {
						struct client_node *client = create_client_node();
						inet_ntop(AF_INET, &(yourhost.sin_addr), buffer, INET_ADDRSTRLEN);
						printf("Incoming connection from %s:%hu\n", buffer, ntohs(yourhost.sin_port));
						
						add_client_node(client);
						client->addr = yourhost;
						client->socket = sock_client;
						client->state = CONNECTED;
						client->read_dispatch = &get_username;
						monitor_socket_r(sock_client);
					} else {
						perror("Errore accept()");
						close(sock_listen);
						return 1;
					}
				} else {
					struct client_node *client;
					sock_client = i;
					
					client = get_client_by_socket(sock_client);
					if ( client != NULL ) {
						client->read_dispatch(client);
					} else { /* non dovrebbe mai succedere */
						unmonitor_socket_r(sock_client);
						unmonitor_socket_w(sock_client);
						shutdown(sock_client, SHUT_RDWR);
						close(sock_client);
					}
				}
				break;
				
			} else if ( FD_ISSET(i, &_writefds) ) {
				struct client_node *client;
				sock_client = i;
				
				client = get_client_by_socket(sock_client);
				if ( client != NULL ) {
					client->write_dispatch(client);
				} else { /* non dovrebbe mai succedere */
					unmonitor_socket_r(sock_client);
					unmonitor_socket_w(sock_client);
					shutdown(sock_client, SHUT_RDWR);
					close(sock_client);
				}
				
				break;
			}
		}
		_readfds = readfds;
		_writefds = writefds;
	}
	
	for ( i = 0; i <= maxfds; i++ ) {
		if ( FD_ISSET(i, &readfds) || FD_ISSET(i, &writefds) ) {
			shutdown(i, SHUT_RDWR);
			close(i);
		}
	}
	
	if ( sel_status == 0 ) puts("Timeout.");
	else perror("Errore su select()");
	
	return 0;
}

void get_username(struct client_node *client) {
	uint8_t cmd;
	received = recv(client->socket, &cmd, 1, 0);
	
	if (received != 1) {
		client_disconnected(client);
		return;
	}
	
	if (cmd != REQ_LOGIN) {
		puts("BADREQ");
		send_byte(client, RESP_BADREQ);
		return;
	}
		
	received = recv(client->socket, &(client->username_len), 1, 0);
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
				printf("Client %s:%hu has username \"%s\"\n", buffer, ntohs(client->addr.sin_port), client->username);
				printf("[%s] Listening on %s:%hu\n", client->username, buffer, client->udp_port);
				
				client->state = FREE;
				client->read_dispatch = &idle_free;
				send_byte(client, RESP_OK_LOGIN);
			} 
		} else client_disconnected(client);
	} else client_disconnected(client);
}

void idle_free(struct client_node *client) {
	uint8_t cmd;
	int total_length;
	struct client_node *cn;
	received = recv(client->socket, &cmd, 1, 0);
	if ( received != 1 ) {
		client_disconnected(client);
		return;
	}
	
	switch ( cmd ) {
		case REQ_WHO:
			total_length = 1 + 4 + client_list.count;
			for (cn = client_list.head; cn != NULL; cn = cn->next )
				total_length += cn->username_len;
			client->data = (char*) malloc(total_length);
			check_alloc(client->data);
			client->data_cursor = 0;
			pack(client->data, "bl", RESP_WHO, client_list.count);
			client->data_count = 5;
			for (cn = client_list.head; cn != NULL; cn = cn->next ) {
				pack(client->data + client->data_count, "bs", client->username_len, client->username);
				client->data_count += client->username_len + 1;
			}
			client->write_dispatch = &send_data;
			monitor_socket_w(client->socket);
			unmonitor_socket_r(client->socket);
			break;
		
		case REQ_PLAY:
		
			break;
		
		default:
			send_byte(client, RESP_BADREQ);
	}
}

void idle_busy(struct client_node *client) {
	
}

void client_disconnected(struct client_node *client) {
	inet_ntop(AF_INET, &(client->addr.sin_addr), buffer, INET_ADDRSTRLEN);
	if ( client->state == FREE || client->state == BUSY )
		printf("[%s] %s:%hu disconnected\n", client->username, buffer, ntohs(client->addr.sin_port));
	else
		printf("[unknown] %s:%hu disconnected\n", buffer, ntohs(client->addr.sin_port));
		
	shutdown(client->socket, SHUT_RDWR);
	close(client->socket);
	unmonitor_socket_r(client->socket);
	unmonitor_socket_w(client->socket);
	destroy_client_node(remove_client_node(client));
}

void send_byte(struct client_node *client, uint8_t byte) {
	client->byte_resp = byte;
	client->data = NULL;
	client->data_count = 1;
	client->data_cursor = 0;
	client->write_dispatch = &send_data;
	unmonitor_socket_r(client->socket);
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
			
			switch (client->state)  {
				case CONNECTED: client->read_dispatch = &get_username; break;
				case FREE: client->read_dispatch = &idle_free; break;
				case BUSY: client->read_dispatch = &idle_busy; break;
				default: client_disconnected(client); return;
			}
			
			monitor_socket_r(client->socket);
			unmonitor_socket_w(client->socket);
		}
	} else client_disconnected(client);
}
