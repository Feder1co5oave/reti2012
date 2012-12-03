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
	/* FD_SET(sock_listen, &readfds);
	update_maxfds(sock_listen); */
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
						/* FD_SET(sock_client, &readfds);
						update_maxfds(sock_client); */
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
					if (client == NULL) {
						FD_CLR(sock_client, &readfds);
						close(sock_client);
					}
					
					switch (client->state) {
						case NONE:
							if ( (received = recv(sock_client, buffer, 1, 0)) == 1) {
								unpack(buffer, "b", &(client->username_len));
								if ( (received = recv(sock_client, buffer, client->username_len + 2, 0)) == client->username_len + 2 ) {
									unpack(buffer, "sw", client->username_len, &(client->username), &(client->udp_port));
									inet_ntop(AF_INET, &(client->addr.sin_addr), buffer, INET_ADDRSTRLEN);
									printf("[user %s] Listening on %s:%hu\n", client->username, buffer, client->udp_port);
								}
							}
							break;
						case CONNECTED:
							break;
						case FREE:
							break;
						case BUSY:
							break;
					}
					
					if ( (received = recv(sock_client, buffer, 4096, 0)) < 0) {
						perror("Errore su recv()");
					} else if (received > 0) {
						buffer[received] = 0;
						printf("%s", buffer);
					} else {
						puts("Client sconnesso.");
					
						shutdown(sock_client, SHUT_RDWR);
						close(sock_client);
						FD_CLR(sock_client, &readfds);
					}
				}
				break;
				
			} else if ( FD_ISSET(i, &_writefds) ) {
				struct client_node *client;
				sock_client = i;
				
				FD_CLR(sock_client, &writefds);
				
				break;
			}
		}
		_readfds = readfds;
		_writefds = writefds;
	}
	
	for ( i = 0; i <= 20; i++ ) {
		if ( FD_ISSET(i, &readfds) || FD_ISSET(i, &writefds) ) {
			shutdown(i, SHUT_RDWR);
			close(i);
		}
	}
	
	if ( sel_status == 0 ) puts("Timeout.");
	else perror("Errore su select()");
	
	return 0;
}
