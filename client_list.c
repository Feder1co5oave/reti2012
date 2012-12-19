#include "client_list.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct client_node *create_client_node() {
	struct client_node *cn = malloc(sizeof(struct client_node));
	if ( cn == NULL ) {
		fprintf(stderr, "Errore su malloc()");
		exit(-1);
	}
	memset(cn, 0, sizeof(struct client_node));
	cn->state = NONE;
	cn->next = NULL;
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
	while ( nc && (strcmp(nc->username, username) != 0 || nc->state == CONNECTED) )
		nc = nc->next;
	return nc;
}
