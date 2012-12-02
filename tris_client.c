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

int main (void) {
	int sock;
	char buf[20];
	struct addrinfo hints, *gai_results, *p;
	u_int16_t port = 3958;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	getaddrinfo("localhost", "4096", &hints, &gai_results);
	p = gai_results;
	freeaddrinfo(gai_results);
	sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
	connect(sock, p->ai_addr, p->ai_addrlen);
	
	port = htons(port);
	buf[0] = 4;
	memcpy(&buf[1], "fede", 4);
	memcpy(&buf[5], &port, 2);
	
	
	send(sock, buf, 7, 0);
	shutdown(sock, SHUT_RDWR);
	close(sock);
	return 0;
}
