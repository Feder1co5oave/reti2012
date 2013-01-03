#include "client_list.h"
#include "log.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>

client_list_t client_list = {NULL, NULL, 0};

struct client_node *create_client_node() {
	struct client_node *cn = malloc(sizeof(struct client_node));
	check_alloc(cn);
	cn->state = NONE;
	cn->next = NULL;
	cn->req_to = NULL;
	cn->req_from = NULL;
	cn->play_with = NULL;
	cn->data = NULL;
	cn->data_count = 0;
	cn->data_cursor = 0;
	cn->read_dispatch = NULL;
	cn->write_dispatch = NULL;
	cn->muted = FALSE;
	return cn;
}

struct client_node *destroy_client_node(struct client_node *cn) {
	struct client_node *ret = cn->next;
	if ( cn->data != NULL ) free(cn->data);
	free(cn);
	return ret;
}

void destroy_client_list(struct client_node *cn) {
	while ( cn != NULL )
		cn = destroy_client_node(cn);
}


void add_client_node(struct client_node *cn) {
	if ( client_list.tail == NULL ) {
		client_list.head = client_list.tail = cn;
	} else {
		client_list.tail->next = cn;
		client_list.tail = cn;
	}
	cn->next = NULL;
	client_list.count++;
}

struct client_node *remove_client_node(struct client_node *cn) {
	struct client_node *ptr;
	
	if ( client_list.head == cn ) client_list.head = cn->next;
	ptr = client_list.head;
	while ( ptr != NULL && ptr->next != cn ) ptr = ptr->next;
	if ( ptr != NULL && ptr->next == cn ) ptr->next = cn->next;
	if ( client_list.tail == cn ) client_list.tail = ptr;
	client_list.count--;
	return ptr;
}

struct client_node *get_client_by_socket(int socket) {
	struct client_node *nc = client_list.head;
	while ( nc && nc->socket != socket ) nc = nc->next;
	return nc;
}

struct client_node *get_client_by_username(const char *username) {
	struct client_node *nc = client_list.head;
	while ( nc &&
		(strcmp(nc->username, username) != 0 || nc->state == CONNECTED) )
		nc = nc->next;
	return nc;
}


/* CLIENT_REPR_SIZE = max(
	strlen("[longest_username]"),
	strlen("123.456.789.123:12345")
) + 1 */
#if MAX_USERNAME_LENGTH + 3 > 22
#define CLIENT_REPR_SIZE MAX_USERNAME_LENGTH + 3
#else
#define CLIENT_REPR_SIZE 22
#endif

char client_repr_buffer[CLIENT_REPR_SIZE];

const char *client_sockaddr_p(struct client_node *client) {
	if ( client != NULL ) {
		const char *s;
		s = inet_ntop(AF_INET, &(client->addr.sin_addr), client_repr_buffer,
			INET_ADDRSTRLEN);
		if ( s == NULL ) log_message(LOG_DEBUG, _("Client has invalid address"));
		sprintf(client_repr_buffer + strlen(client_repr_buffer), ":%hu",
			ntohs(client->addr.sin_port));
		return client_repr_buffer;
	} else return NULL;
}

const char *client_canon_p(struct client_node *client) {
	if ( client != NULL ) {
		switch ( client->state ) {
			case NONE:
			case CONNECTED:
				return client_sockaddr_p(client);
			case FREE:
			case BUSY:
			case PLAY:
				sprintf(client_repr_buffer, "[%s]", client->username);
		}

		return client_repr_buffer;
	} else {
		return NULL;
	}
}

int log_statechange(struct client_node *client) {
	if ( client != NULL ) return flog_message(LOG_DEBUG, "%s is now %s", client_canon_p(client), state_name(client->state));
	
	log_message(LOG_WARNING, "Client is NULL in log_statechange");
	return 0;
}
