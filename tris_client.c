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

int main (int argc, char **argv) {
	int sock;
	char buf[20];
	struct addrinfo hints, *gai_results, *p;
	uint16_t port = 3958;
	uint8_t resp;
	uint32_t size;
	int i, length;
	
	if (argc != 2) {
		puts("Usage: tris_client <username>");
		return 1;
	}

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	getaddrinfo("127.0.0.1", "4096", &hints, &gai_results);
	p = gai_results;
	freeaddrinfo(gai_results);
	sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
	connect(sock, p->ai_addr, p->ai_addrlen);
	
	size = pack(buf, "bbsw", REQ_LOGIN, (uint8_t) strlen(argv[1]), argv[1], port);
	send(sock, buf, 4 + strlen(argv[1]), 0);
	recv(sock, &resp, 1, 0);
	if ( resp == RESP_OK_LOGIN ) puts("Connesso e loggato");
	else if ( resp == RESP_BADUSR ) puts("Username non valido");
	else if ( resp == RESP_EXIST ) puts("Username già in uso");

	buf[0] = REQ_WHO;
	send(sock, buf, 1, 0);
	recv(sock, &resp, 1, 0);
	if ( resp == RESP_WHO ) {
		recv(sock, buf, 4, 0);
		unpack(buf, "l", &size);
		printf("Ci sono %d client connessi\n", size);
		if (size > 10) return 1;
		for (i = 0; i < size; i++) {
			recv(sock, &length, 1, 0);
			recv(sock, buf, length, 0);
			buf[length] = '\0';
			printf("Utente %s\n", buf);
		}
	} else if ( resp == RESP_BADREQ ) puts("BADREQ");
	
	getchar();
	
	shutdown(sock, SHUT_RDWR);
	close(sock);
	return 0;
}
