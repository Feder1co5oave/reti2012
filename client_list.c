#include "client_list.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

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
		if (cn->data != NULL) free(cn->data);
		free(cn);
		cn = cn2;
		cn2 = cn->next;
	}
}



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
