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

char buffer[4097];

int main (int argc, char **argv) {
	struct sockaddr_in myhost;
	int sock_listen, yes = 1, sel_status, i, received;
	struct sockaddr_storage yourhost;
	int sock_client, addrlen = sizeof(yourhost);
	fd_set readfds, writefds, _readfds, _writefds;
	struct timeval tv = {20, 0};
	
	FD_ZERO(&readfds);
	FD_ZERO(&writefds);
	
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
	
	FD_SET(sock_listen, &readfds);
	_readfds = readfds;
	_writefds = writefds;
	
	
	while ( (sel_status = select(20, &_readfds, &_writefds, NULL, &tv)) > 0 ) {
		for (i = 0; i <= 20; i++) {
			if ( FD_ISSET(i, &_readfds) ) { // i == sock_listen
				if (i == sock_listen) {
					
					if ( (sock_client = accept(sock_listen, (struct sockaddr*)&yourhost, &addrlen)) != -1 ) {
						FD_SET(sock_client, &writefds);
						FD_SET(sock_client, &readfds);
					} else {
						perror("Errore accept()");
						close(sock_listen);
						return 1;
					}
					
				} else {
					
					sock_client = i;
					
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
	
	for (i = 0; i <= 20; i++) {
		if ( FD_ISSET(i, &readfds) || FD_ISSET(i, &writefds) ) {
			shutdown(i, SHUT_RDWR);
			close(i);
		}
	}
	
	if (sel_status == 0) puts("Timeout.");
	else perror("Errore su select()");
	
	//close(sock_listen);

	return 0;
}
