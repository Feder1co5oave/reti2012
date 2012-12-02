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

#define BACKLOG 5
#define MAX_USERNAME_LENGTH 30

#define update_maxfds(n) maxfds = (maxfds < (n) ? (n) : maxfds)

enum client_state { NONE, CONNECTED, FREE, BUSY };

struct client_node {
	char username_len;
	char username[MAX_USERNAME_LENGTH];
	int socket;
	struct sockaddr_in addr;
	u_int16_t udp_port;
	enum client_state state;
	struct client_node *next;
};

struct client_node *create_client_node() {
	struct client_node *cn = (struct client_node*) malloc(sizeof(struct client_node));
	if ( cn == NULL ) {
		fprintf(stderr, "Errore su malloc()");
		exit(-1);
	}
	memset(cn, 0, sizeof(struct client_node));
	cn->state = NONE;
	cn->next = NULL;
	return cn;
}

void destroy_client_node(struct client_node *cn) {
	struct client_node *cn2 = cn->next;
	while ( cn != NULL ) {
		free(cn);
		cn = cn2;
		cn2 = cn->next;
	}
}

struct {
	struct client_node *head, *tail;
} client_list = {NULL, NULL};

void add_client_node(struct client_node *cn) {
	if ( client_list.tail == NULL ) {
		client_list.head = client_list.tail = cn;
	} else {
		client_list.tail->next = cn;
		client_list.tail = cn;
	}
	cn->next = NULL;
}

struct client_node *remove_client_node(struct client_node *cn) {
	struct client_node *ptr;
	
	if ( client_list.head == cn ) client_list.head = cn->next;
	ptr = client_list.head;
	while ( ptr->next != cn ) ptr = ptr->next;
	ptr->next = cn->next;
	if ( cn == client_list.tail ) client_list.tail = ptr;
	
	return cn;
}

struct client_node *get_client_by_socket(int socket) {
	struct client_node *nc = client_list.head;
	while (nc && nc->socket != socket) nc = nc->next;
	return nc;
}

char buffer[4097];

int main (int argc, char **argv) {
	struct sockaddr_in myhost;
	int sock_listen;
	
	int yes = 1, sel_status, i, received;
	
	struct sockaddr_in yourhost;
	int sock_client, addrlen = sizeof(yourhost);
	
	fd_set readfds, writefds, _readfds, _writefds;
	int maxfds = -1;
	struct timeval tv = {60, 0};
	
	
	
	if ( argc != 3 /*|| strlen(argv[1]) < 7 || strlen(argv[1]) > 15 || strlen(argv[2]) > 5*/ ) {
		puts("Usage: tris_server <host> <porta>");
		return 1;
	}
	
	if ( inet_pton(AF_INET, argv[1], &(myhost.sin_addr)) != 1 ) {
		puts("Indirizzo host non valido");
		return 1;
	}
	
	myhost.sin_family = AF_INET;
	myhost.sin_port = htons((u_int16_t) atoi(argv[2]));
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
	
	FD_ZERO(&readfds);
	FD_ZERO(&writefds);
	FD_SET(sock_listen, &readfds);
	update_maxfds(sock_listen);
	_readfds = readfds;
	_writefds = writefds;
	
	
	while ( (sel_status = select(maxfds + 1, &_readfds, &_writefds, NULL, &tv)) > 0 ) {
		for ( i = 0; i <= maxfds; i++ ) {
			if ( FD_ISSET(i, &_readfds) ) {
				if ( i == sock_listen ) {
					
					if ( (sock_client = accept(sock_listen, (struct sockaddr*)&yourhost, &addrlen)) != -1 ) {
						struct client_node *nc = create_client_node();
						inet_ntop(AF_INET, &(yourhost.sin_addr), buffer, INET_ADDRSTRLEN);
						printf("[user unknown] %s:%hu connected\n", buffer, ntohs(yourhost.sin_port));
						
						add_client_node(nc);
						nc->addr = yourhost;
						nc->socket = sock_client;
						FD_SET(sock_client, &readfds);
						update_maxfds(sock_client);
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
								client->username_len = buffer[0];
								if ( (received = recv(sock_client, buffer, client->username_len + 2, 0)) == client->username_len + 2 ) {
									memcpy(&(client->username), buffer, client->username_len);
									client->username[client->username_len] = '\0';
									memcpy(&(client->udp_port), &buffer[client->username_len], 2);
									client->udp_port = ntohs(client->udp_port);
									printf("[user %s] Listening on port %hu\n", client->username, client->udp_port);
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
				
				sock_client = i;
				if ( send(sock_client, "Hello world!\n", 13, 0) != 13 ) {
					perror("Errore send()");
				}
				
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
