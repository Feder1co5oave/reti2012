#ifndef _CLIENT_LIST_H
#define _CLIENT_LIST_H

#include "common.h"

#include <netinet/in.h>



/* ===[ Data types ]========================================================= */

/**
 * Represents a client on the server.
 */
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

/**
 * A list of client_nodes.
 */
typedef struct {
	struct client_node *head, *tail;
	int count;
} client_list_t;



/* ===[ Data ]=============================================================== */

extern client_list_t client_list;



/* ===[ Functions ]========================================================== */

/**
 * Allocate and initialize a new client_node.
 * @return the new client_node
 */
struct client_node *create_client_node(void);

/**
 * Deallocate a client_node and any data within it.
 * @return the ->next member of the now dead client_node
 */
struct client_node *destroy_client_node(struct client_node*);

/**
 * Deallocate all client_nodes in a list, one by one.
 */
void destroy_client_list(struct client_node*);

/**
 * Append a client_node to client_list.
 */
void add_client_node(struct client_node*);

/**
 * Remove a client_node from client_list.
 * @return a pointer to the client_node *preceding* it in client_list
 */
struct client_node *remove_client_node(struct client_node*);

/**
 * @return the first client_node in client_list that matches the passed socket,
 * or NULL if there aren't any
 */
struct client_node *get_client_by_socket(int socket);

/**
 * @return the first client_node in client_list that matches the passed username,
 * or NULL if there aren't any
 */
struct client_node *get_client_by_username(const char *username);

/**
 * Get a string containing the "ip:port" representation of the sockaddr of a
 * client.
 * @return a pointer to the string (static data) or NULL on error
 */
const char *client_sockaddr_p(struct client_node*);

/**
 * Get the canonical representation of a client, i.e. "[username]" for clients
 * in state FREE, BUSY or PLAY, or "ip:port" for CONNECTED (and NONE).
 * @return a pointer to the string (static data) or NULL on error
 */
const char *client_canon_p(struct client_node*);

/**
 * Logs a change of state for a client, as LOG_DEBUG.
 */
int log_statechange(struct client_node*);

/* ========================================================================== */

#endif
