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

int main (void) {
	int sock;
	char buf[20];
	struct addrinfo hints, *gai_results, *p;
	uint16_t port = 3958;
	uint8_t resp;
	int size, i, length;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	getaddrinfo("127.0.0.1", "4096", &hints, &gai_results);
	p = gai_results;
	freeaddrinfo(gai_results);
	sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
	connect(sock, p->ai_addr, p->ai_addrlen);
	
	size = pack(buf, "bsw", (uint8_t) 4, "fede", port);
	send(sock, buf, 7, 0);
	puts("Connesso e loggato");
	
	buf[0] = REQ_WHO;
	send(sock, buf, 1, 0);
	
	recv(sock, buf, 5, 0);
	unpack(buf, "bl", &resp, &size);
	printf("Ci sono %d client connessi\n", size);
	for (i = 0; i < size; i++) {
		recv(sock, &length, 1, 0);
		recv(sock, buf, length, 0);
		buf[length] = '\0';
		printf("Utente %s\n", buf);
	}
	
	getchar();
	
	shutdown(sock, SHUT_RDWR);
	close(sock);
	return 0;
}
