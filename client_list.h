#ifndef _CLIENT_LIST_H
#define _CLIENT_LIST_H

#include "common.h"

#include <netinet/in.h>

struct client_node {
	char username_len;
	char username[MAX_USERNAME_LENGTH + 1];
	int socket;
	enum client_state state;
	struct sockaddr_in addr;
	uint16_t udp_port;
	uint8_t byte_resp;
	struct client_node *next, *req_to, *req_from, *play_with;
	char *data;
	int data_count, data_cursor;
	void (*read_dispatch)(struct client_node*);
	void (*write_dispatch)(struct client_node*);
};

struct client_node *create_client_node(void);

struct client_node *destroy_client_node(struct client_node*);

void destroy_client_list(struct client_node*);

struct {
	struct client_node *head, *tail;
	int count;
} client_list;

void add_client_node(struct client_node*);

struct client_node *remove_client_node(struct client_node*);

struct client_node *get_client_by_socket(int socket);

struct client_node *get_client_by_username(const char *username);

const char *client_sockaddr_p(struct client_node*);

const char *client_canon_p(struct client_node*);

#endif
