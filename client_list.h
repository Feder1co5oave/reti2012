#ifndef _CLIENT_LIST_H
#define _CLIENT_LIST_H

#include "common.h"

#include <netinet/in.h>

enum client_state { NONE, CONNECTED, FREE, BUSY };

struct client_node {
	char username_len;
	char username[MAX_USERNAME_LENGTH];
	int socket;
	struct sockaddr_in addr;
	uint16_t udp_port;
	enum client_state state;
	struct client_node *next;
	char *data;
	int data_count, data_cursor;
	void (*read_dispatch)(struct client_node*), (*write_dispatch)(struct client_node*);
};

struct client_node *create_client_node();

void destroy_client_node(struct client_node *cn);

struct {
	struct client_node *head, *tail;
	int count;
} client_list;

void add_client_node(struct client_node *cn);

struct client_node *remove_client_node(struct client_node *cn);

struct client_node *get_client_by_socket(int socket);

#endif
