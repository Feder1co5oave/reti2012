#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BACKLOG 5


typedef struct {
	char *username;
	int socket;
	struct addrinfo address;
	enum State state;
} tris_client_t;






int main (int argc, char **argv) {
	struct sockaddr_in myhost;
	int sock_listen, yes = 1, pid, status;
	
	struct sockaddr_storage yourhost;
	int sock_client, addrlen = sizeof(yourhost);
	
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
	
	while ( (sock_client = accept(sock_listen, (struct sockaddr*)&yourhost, &addrlen)) != -1 ) {
		pid = fork();
		
		if ( pid == 0 ) { // child process
			if ( send(sock_client, "Hello world!\n", 13, 0) != 13 ) {
				perror("Errore send()");
			}
			
			shutdown(sock_client, SHUT_RDWR);
			close(sock_client);
			
			_exit(0);
		} else if ( pid < 0 ) {
		
			perror("Errore fork()");
		}
	}
	
	perror("Errore accept()");
	close(sock_listen);
	
	while ( wait(&status) > 0 ); // wait death of child processes

	return 0;
}
