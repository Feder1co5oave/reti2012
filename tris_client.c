#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/poll.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "common.h"
#include "log.h"
#include "tris_game.h"

#define PROMPT_FREE '>'
#define PROMPT_PLAY '#'

#define update_maxfds(n) maxfds = (maxfds < (n) ? (n) : maxfds)
#define monitor_socket_r(sock) { FD_SET(sock, &readfds); update_maxfds(sock); }
#define monitor_socket_w(sock) { FD_SET(sock, &writefds); update_maxfds(sock); }
#define unmonitor_socket_r(sock) FD_CLR(sock, &readfds)
#define unmonitor_socket_w(sock) FD_CLR(sock, &writefds)
#define log_statechange() \
                    flog_message(LOG_DEBUG, "Ora sono %s", state_name(my_state))

#define print_ip(host) \
      inet_ntop(AF_INET, &((host).sin_addr), buffer, sizeof(struct sockaddr_in))



/* ===[ Helpers ]============================================================ */

bool connect_play_socket(struct sockaddr_in *host);
void end_match(bool send_opp);
void free_shell(void);
bool get_hello(void);
void get_play_response(void);
void got_hit_or_end(void);
void got_play_request(void);
void list_connected_clients(void);
void login(void);
void make_move(unsigned int cell, bool send_opp);
bool open_play_socket(void);
void play_shell(void);
int poll_in(int socket, int timeout);
bool say_hello(void);
void send_play_request(void);
void server_disconnected(void);
void start_match(char me);



/* ===[ Data ]=============================================================== */

char buffer[BUFFER_SIZE];

fd_set readfds, writefds;
int maxfds = -1;
struct log_file *console;

struct sockaddr_in my_host;
char               my_username[MAX_USERNAME_LENGTH + 1];
enum client_state  my_state = NONE;

struct sockaddr_in opp_host;
int                opp_socket;
char               opp_username[MAX_USERNAME_LENGTH + 1];

int sock_server, error;

struct tris_grid grid = TRIS_GRID_INIT;
char my_player, turn;



/* ===[ Main ]=============================================================== */

int main (int argc, char **argv) {
	struct timeval tv = DEFAULT_TIMEOUT_INIT, _tv;
	struct sockaddr_in server_host;
	socklen_t sockaddrlen = sizeof(struct sockaddr_in);
	fd_set _readfds, _writefds;
	int sel_status;

	/* Set log files */
	console = new_log(stdout, LOG_CONSOLE | LOG_INFO | LOG_ERROR, FALSE);
	sprintf(buffer, "logs/tris_client-%d.log", getpid());
	open_log(buffer, LOG_ALL);
	
	if (argc != 3) {
		flog_message(LOG_CONSOLE, "Utilizzo: %s <ip_server> <porta_server>",
                                                                       argv[0]);
		
		exit(EXIT_FAILURE);
	}
	
	if ( inet_pton(AF_INET, argv[1], &(server_host.sin_addr)) != 1 ) {
		log_message(LOG_CONSOLE, "Indirizzo server non valido");
		exit(EXIT_FAILURE);
	}
	
	server_host.sin_family = AF_INET;
	server_host.sin_port = htons((uint16_t) atoi(argv[2]));
	memset(server_host.sin_zero, 0, sizeof(server_host.sin_zero));

	sock_server = socket(server_host.sin_family, SOCK_STREAM , 0);
	if ( sock_server == -1 ) {
		log_error("Errore socket()");
		exit(EXIT_FAILURE);
	}
	
	if ( connect(sock_server, (struct sockaddr*) &server_host,
                                                   sizeof(server_host)) != 0 ) {
		log_error("Errore connect()");
		exit(EXIT_FAILURE);
	}
	
	if ( getsockname(sock_server, (struct sockaddr*) &my_host, &sockaddrlen) !=
                               0 || sockaddrlen > sizeof(struct sockaddr_in) ) {
		
		log_error("Errore getsockname()");
		exit(EXIT_FAILURE);
	}
	
	print_ip(my_host);
	flog_message(LOG_DEBUG, "Sono l'host %s:%hu", buffer,
                                                       ntohs(my_host.sin_port));
	
	my_state = CONNECTED;
	log_message(LOG_CONSOLE, "Connessione al server avvenuta con successo");
	log_statechange();
	
	login();
	
	console->prompt = PROMPT_FREE;
	log_prompt(console);
	
	FD_ZERO(&readfds);
	FD_ZERO(&writefds);
	monitor_socket_r(STDIN_FILENO);
	monitor_socket_r(sock_server);
	_readfds = readfds;
	_writefds = writefds;
	_tv = tv;
	
	
	while ( (sel_status = select(maxfds + 1, &_readfds, &_writefds, NULL, &_tv))
                                                                        >= 0 ) {
		
		if ( sel_status == 0 ) {
			flog_message(LOG_DEBUG, "Timeout per select() mentre %s",
                                                          state_name(my_state));
			
			if ( my_state == PLAY ) {
				log_message(LOG_INFO, "Timeout: termino la partita...");
				end_match(FALSE);
				log_prompt(console);
			}
			
			_readfds = readfds;
			_writefds = writefds;
			_tv = tv;
			continue;
		}
		
		if ( FD_ISSET(STDIN_FILENO, &_readfds) ) {
			
			switch ( my_state ) {
				case FREE: free_shell(); break;
				case PLAY: play_shell(); break;
				default:
					get_line(buffer, BUFFER_SIZE);
					log_message(LOG_USERINPUT, buffer);
					flog_message(LOG_WARNING,
                                        "Ricevuto input indesiderato mentre %s",
                                                          state_name(my_state));
					
					log_message(LOG_ERROR, "");
			}
			log_prompt(console);
			
		} else if ( FD_ISSET(sock_server, &_readfds) ) {
			
			uint8_t cmd;
			
			if ( recv(sock_server, &cmd, 1, 0) != 1 )
				server_disconnected();
			
			switch ( cmd ) {
				case REQ_PLAY:
					flog_message(LOG_DEBUG,
				                       "Ricevuto REQ_PLAY dal server mentre %s",
                                                          state_name(my_state));
					
					if ( my_state == FREE ) {
						got_play_request();
						log_prompt(console);
					} else if ( send_byte(sock_server, RESP_REFUSE) < 0 )
						server_disconnected();
					break;
				
				case RESP_OK_PLAY:
					recv(sock_server, buffer, sizeof(struct in_addr) + 2, 0);
				case RESP_BUSY:
				case RESP_NONEXIST:
				case RESP_REFUSE:
					flog_message(LOG_INFO_VERBOSE,
                  "Ricevuta in ritardo la risposta=%s a una richiesta di gioco",
                                                               magic_name(cmd));
					
					break;
				default:
					flog_message(LOG_WARNING, "Comando dal server inatteso: %s",
                                                               magic_name(cmd));
				
					if ( cmd != RESP_BADREQ &&
                                       send_byte(sock_server, RESP_BADREQ) < 0 )
					
						server_disconnected();
			}
			
		} else if ( my_state == PLAY && FD_ISSET(opp_socket, &_readfds) ) {
			
			got_hit_or_end();
			log_prompt(console);
			
		} else {
			flog_message(LOG_WARNING, "Evento inatteso alla linea %d mentre %s",
                                                __LINE__, state_name(my_state));
			
			sleep(1);
		}
		
		_readfds = readfds;
		_writefds = writefds;
		_tv = tv;
	}

	log_error("Error select()");
	shutdown(sock_server, SHUT_RDWR);
	close(sock_server);
	exit(EXIT_FAILURE);
}

/* ========================================================================== */

void free_shell() {
	char cmd[BUFFER_SIZE] = "";
	
	get_line(buffer, BUFFER_SIZE);
	sscanf(buffer, "%s", cmd);
	log_message(LOG_USERINPUT, buffer);
	
	if ( strcmp(buffer, "!help") == 0 ) { /* ------------------------ > !help */
		
		log_multiline(LOG_CONSOLE,
"Sono disponibili i seguenti comandi:\n"
" * !help --> mostra l'elenco dei comandi disponibili\n"
" * !who --> mostra l'elenco dei client connessi al server\n"
" * !connect <player> --> avvia una partita con il client di nome <player>\n"
" * !quit --> termina il programma");
		
	} else if ( strcmp(buffer, "!who") == 0 ) { /* ------------------- > !who */
		
		list_connected_clients();
		
	} else if ( strcmp(cmd, "!connect") == 0 ) { /* -------------- > !connect */
		
		char username[50];
		int s, username_length;
		
		s = sscanf(buffer, "!connect %s", username);
		username_length = strlen(username);
		
		if ( username_length <= 0 || s != 1 ) {
			log_message(LOG_CONSOLE, "Sintassi: !connect <nome_giocatore>");
			return;
		}
		
		if ( username_length > MAX_USERNAME_LENGTH ) {
			log_message(LOG_CONSOLE, "Questo username è troppo lungo");
			return;
		}
		
		if ( username_length < MIN_USERNAME_LENGTH ) {
			log_message(LOG_CONSOLE, "Questo username è troppo corto");
			return;
		}
		
		if ( !username_is_valid(username, username_length) ) {
			log_message(LOG_CONSOLE, "Questo username è malformato");
			return;
		}
		
		if ( strcmp(username, my_username) == 0 ) {
			log_message(LOG_CONSOLE,"Non puoi giocare con te stesso");
			return;
		}
		
		strcpy(opp_username, username);
		send_play_request();
		get_play_response();
		
	} else if ( strcmp(buffer, "!quit") == 0 ) { /* ----------------- > !quit */
		
		shutdown(sock_server, SHUT_RDWR);
		close(sock_server);
		exit(EXIT_SUCCESS);
		
	} else if ( strcmp(buffer, "") != 0 ) {
		
		log_message(LOG_CONSOLE, "Comando sconosciuto. Digita '!help' per la "
                                               "lista dei comandi disponibili");
		
	}
}

bool open_play_socket() {
	opp_socket = socket(AF_INET, SOCK_DGRAM, 0);
	if ( opp_socket == -1 ) {
		log_error("Errore socket(SOCK_DGRAM)");
		return FALSE;
	}
	
	log_message(LOG_DEBUG, "Socket di gioco aperto");
	
	if ( bind(opp_socket, (struct sockaddr*) &my_host, sizeof(my_host)) != 0 ) {
		
		log_error("Errore bind(SOCK_DGRAM)");
		close(opp_socket);
		opp_socket = -1;
		return FALSE;
	}
	
	print_ip(my_host);
	flog_message(LOG_DEBUG, "Socket di gioco associato a %s:%hu", buffer,
                                                       ntohs(my_host.sin_port));
	
	return TRUE;
}

bool connect_play_socket(struct sockaddr_in *host) {
	struct sockaddr_in null_host;
	
	if ( host == NULL ) {
		memset(&null_host, 0, sizeof(null_host));
		null_host.sin_family = AF_UNSPEC;
		host = &null_host;
	}
	
	if ( connect(opp_socket, (struct sockaddr*) host, sizeof(*host)) != 0 ) {
		log_error("Errore connect(SOCK_DGRAM)");
		return FALSE;
	}
	
	print_ip(*host);
	flog_message(LOG_DEBUG, "Socket di gioco connesso a %s:%hu", buffer,
                                                         ntohs(host->sin_port));
	
	return TRUE;
}

void end_match(bool send_opp) {
	uint8_t resp;
	
	if ( send_opp && send_byte(opp_socket, REQ_END) < 0 )
		log_error("Errore send()");
	
	if ( send_byte(sock_server, REQ_END) < 0 ) server_disconnected();
	if ( recv(sock_server, &resp, 1, 0) != 1 ) server_disconnected();
	
	my_state = FREE;
	log_statechange();
	
	if ( resp != RESP_OK_FREE )
		flog_message(LOG_WARNING, "Risposta dal server inattesa: %s",
                                                              magic_name(resp));
	
	log_message(LOG_CONSOLE, "Fine della partita");
	
	if ( !connect_play_socket(&my_host) )
		log_message(LOG_WARNING,
                          "Impossibile fare il de-connect del socket di gioco");
	
	if ( opp_socket > 0 ) unmonitor_socket_r(opp_socket);
	
	memset(&opp_host, 0, sizeof(opp_host));
	opp_username[0] = '\0';
	console->prompt = PROMPT_FREE;
}

void login() {
	int username_length;
	uint8_t resp;
	unsigned int my_udp_port;
	
	do {
		log_message(LOG_CONSOLE, "Inserisci lo username:");
		username_length = get_line(buffer, BUFFER_SIZE);
		log_message(LOG_USERINPUT, buffer);
		
		if ( username_length > MAX_USERNAME_LENGTH ) {
			log_message(LOG_CONSOLE, "Questo username è troppo lungo");
			continue;
		}
		
		if ( username_length < MIN_USERNAME_LENGTH ) {
			log_message(LOG_CONSOLE, "Questo username è troppo corto");
			continue;
		}
		
		if ( !username_is_valid(buffer, username_length) ) {
			log_message(LOG_CONSOLE, "Questo username è malformato");
			continue;
		}
		
		strcpy(my_username, buffer);
		
		do {
			log_message(LOG_CONSOLE, "Inserisci la porta UDP di ascolto:");
			get_line(buffer, BUFFER_SIZE);
			log_message(LOG_USERINPUT, buffer);
			if ( sscanf(buffer, " %u", &my_udp_port) != 1 ) continue;
			
			if ( my_udp_port == 0 || my_udp_port >= (1 << 16) ) {
				log_message(LOG_CONSOLE, "La porta UDP specificata non è "
                                 "valida, inserisci un numero da 1024 a 65535");
				
				continue;
			}
			
			if ( my_udp_port < (1 << 10) )
				log_message(LOG_INFO, "La porta UDP specificata è nel range "
                 "delle porte di sistema, potrebbe provocare malfunzionamenti");
			
			my_host.sin_port = htons((uint16_t) my_udp_port);
			
			if ( !open_play_socket() ) {
				log_message(LOG_CONSOLE,
                            "Questa porta UDP è già in uso, scegline un'altra");
				
				continue;
			}
			
			break;
		} while (TRUE);
		
		pack(buffer, "bbsw", REQ_LOGIN, (uint8_t) username_length, my_username,
                                                        (uint16_t) my_udp_port);
		
		
		if ( send_buffer(sock_server, buffer, 4 + username_length) < 0 )
			server_disconnected();
		
		if ( recv(sock_server, &resp, 1, 0) != 1 )
			server_disconnected();
		
		switch ( resp ) {
			case RESP_OK_LOGIN:
				my_state = FREE;
				flog_message(LOG_CONSOLE, "Loggato con successo come '%s'",
                                                                   my_username);
				
				flog_message(LOG_INFO_VERBOSE, "La porta UDP di ascolto è %hu",
                                                       ntohs(my_host.sin_port));
				
				break;
			
			case RESP_EXIST:
				log_message(LOG_CONSOLE | LOG_WARNING,
                             "Questo username è già in uso, scegline un altro");
				
				break;
			
			case RESP_BADUSR:
				log_message(LOG_CONSOLE | LOG_WARNING, "Questo username è "
                       "malformato, sono ammessi caratteri alfanumerici e _.-");
				
				break;
			
			default:
				flog_message(LOG_WARNING, "Risposta dal server inattesa: %s",
                                                              magic_name(resp));
		}
	} while ( resp != RESP_OK_LOGIN );
}

void play_shell() {
	char cmd[BUFFER_SIZE] = "";
	
	get_line(buffer, BUFFER_SIZE);
	sscanf(buffer, "%s", cmd);
	log_message(LOG_USERINPUT, buffer);
	
	if ( strcmp(buffer, "!help") == 0 ) { /* ------------------------ > !help */
		
		log_multiline(LOG_CONSOLE,
"Sono disponibili i seguenti comandi:\n"
" * !help --> mostra l'elenco dei comandi disponibili\n"
" * !who --> mostra l'elenco dei client connessi al server\n"
" * !disconnect --> abbandona la partita in corso con un altro peer\n"
" * !quit --> termina il programma dopo aver abbandonato la partita in corso\n"
" * !show_map --> mostra la mappa di gioco\n"
" * !hit <cell> --> marca la casella <cell>");
		
	} else if ( strcmp(buffer, "!who") == 0 ) { /* ------------------- > !who */
		
		list_connected_clients();
		
	} else if ( strcmp(buffer, "!disconnect") == 0 ) { /* ----- > !disconnect */
		
		log_message(LOG_CONSOLE, "Ti sei arreso!");
		end_match(TRUE);
		
	} else if ( strcmp(buffer, "!quit") == 0 ) { /* ----------------- > !quit */
		
		end_match(TRUE);
		shutdown(sock_server, SHUT_RDWR);
		close(sock_server);
		exit(EXIT_SUCCESS);
		
	} else if ( strcmp(cmd, "!hit") == 0 ) { /* ---------------------- > !hit */
		
		unsigned int cell;
		
		if ( sscanf(buffer, "!hit %u", &cell) == 1 && cell >= 1 && cell <= 9 ) {
			
			if ( turn != my_player ) {
				log_message(LOG_CONSOLE,
                        "Non è il tuo turno, attendi la mossa dell'avversario");
				
				return;
			}
			
			if ( grid.cells[cell] != GAME_UNDEF ) {
				log_message(LOG_CONSOLE, "Questa casella è già occupata");
				return;
			}
			
			make_move(cell, TRUE);
			
		} else {
			struct tris_grid map = TRIS_GRID_MAP_INIT;
			sprintgrid(buffer, &map, "", BUFFER_SIZE);
			log_message(LOG_CONSOLE, "Sintassi: !hit <n>, dove <n> è 1-9:");
			log_multiline(LOG_CONSOLE, buffer);
		}
		
	} else if ( strcmp(buffer, "!show_map") == 0 ) { /* --------- > !show_map */
		
		sprintgrid(buffer, &grid, "", BUFFER_SIZE);
		log_multiline(LOG_CONSOLE, buffer);
		if ( turn == my_player )
			flog_message(LOG_CONSOLE, "E' il tuo turno (%c)", turn);
		else
			flog_message(LOG_CONSOLE, "E' il turno di %s (%c)", opp_username, turn);
		
	} else if ( strcmp(buffer, "!cheat") == 0 ) { /* --------------- > !cheat */
		
		int move;
		
		if ( turn != my_player ) {
			log_message(LOG_CONSOLE,
                        "Non è il tuo turno, attendi la mossa dell'avversario");
			
			return;
		}
		
		backtrack(&grid, my_player, &move);
		flog_message(LOG_CONSOLE, "Marco la casella %d...", move);
		make_move(move, TRUE);
		
	} else if ( strcmp(buffer, "") != 0 ) {
		
		log_message(LOG_CONSOLE, "Comando sconosciuto. Digita '!help' per la "
                                               "lista dei comandi disponibili");
	}
}

bool say_hello() {
	int c, seed;
	
	log_message(LOG_DEBUG, "Sto per contattare l'altro client...");
	
	/* Generate seed */
	srand(time(NULL));
	c = (rand() % 102394) / 1059; /* c in [0, 96] */
	for ( ; c >= 0; c-- ) seed = rand();
	grid.seed = seed;
	update_hash(&grid);
	
	flog_message(LOG_DEBUG, "Il seed per questa partita è %08x", seed);
	
	pack(buffer, "bl", REQ_HELLO, seed);
	if ( send_buffer(opp_socket, buffer, 5) < 0 ) {
		log_error("Errore send()");
		return FALSE;
	}
	
	return TRUE;
}

void got_hit_or_end() {
	uint8_t byte, move;
	uint32_t hash;
	int received;
	
	received = recv(opp_socket, buffer, 6, 0);
	flog_message(LOG_DEBUG, "Ricevuti=%d alla linea %d", received, __LINE__);
	
	if ( received < 0 ) {
		log_error("Errore recv()");
		end_match(FALSE);
		return;
	}
	
	unpack(buffer, "bbl", &byte, &move, &hash);
	
	flog_message(LOG_DEBUG, "Ricevuto %s dall'avversario", magic_name(byte));
	
	if ( byte == REQ_HIT ) {
		
		if ( received != 6 ) {
			flog_message(LOG_WARNING, "Evento inatteso alla linea %d", __LINE__);
			return;
		}
		
		if ( turn == my_player ) {
			flog_message(LOG_WARNING, "Evento inatteso alla linea %d", __LINE__);
			return;
		}
		
		if ( move < 1 || move > 9 ) {
			flog_message(LOG_WARNING, "Evento inatteso alla linea %d", __LINE__);
			return;
		}
		
		if ( grid.cells[move] != GAME_UNDEF ) {
			flog_message(LOG_WARNING, "Evento inatteso alla linea %d", __LINE__);
			return;
		}
		
		make_move(move, FALSE);
		
		if ( grid.hash != hash ) {
			log_message(LOG_ERROR,
                          "Errore: hash errato! La mappa di gioco è corrotta.");
			
			end_match(FALSE);
		}
		
	} else if ( byte == REQ_END ) {
		
		log_message(LOG_INFO, "L'avversario si è arreso: hai vinto!");
		end_match(FALSE);
		
	} else flog_message(LOG_WARNING, "Evento inatteso alla linea %d, byte=%s",
                                                    __LINE__, magic_name(byte));
}

void got_play_request() {
	uint8_t length;
	
	my_state = BUSY;
	
	if ( recv(sock_server, &length, 1, 0) != 1 ) server_disconnected();
	if ( recv(sock_server, buffer, length, 0) != length ) server_disconnected();
	
	buffer[length] = '\0';
	strcpy(opp_username, buffer);
	flog_message(LOG_INFO, "Ricevuta richiesta di gioco da '%s'. "
                                   "Accetta (s) o rifiuta (n) ?", opp_username);
	
	do {
		get_line(buffer, BUFFER_SIZE);
		log_message(LOG_USERINPUT, buffer);

		if ( strcmp(buffer, "s") == 0 ) {
			
			if ( !connect_play_socket(NULL) ) {
				log_message(LOG_CONSOLE,
                    "Richiesta automaticamente rifiutata a causa di un errore");
				
				if ( send_byte(sock_server, RESP_REFUSE) < 0 )
					server_disconnected();
				
				end_match(FALSE);
				return;
			}
			
			if ( send_byte(sock_server, RESP_OK_PLAY) < 0 )
				server_disconnected();
			
			my_state = BUSY;
			log_message(LOG_CONSOLE, "Richiesta accettata, in attesa di "
                                            "connessione dall'altro client...");
			
			if ( get_hello() && connect_play_socket(&opp_host) )
				start_match(GAME_GUEST);
			else
				end_match(FALSE);
			
			break;
			
		} else if ( strcmp(buffer, "n") == 0 ) {
			
			if ( send_byte(sock_server, RESP_REFUSE) < 0 )
				server_disconnected();
			
			my_state = FREE;
			console->prompt = PROMPT_FREE;
			log_message(LOG_CONSOLE, "Richiesta rifiutata");
			break;
			
		} else {
			
			log_message(LOG_CONSOLE, "Accetta (s) o rifiuta (n) ?");
			
		}
	} while (TRUE);
}

bool get_hello() {
	uint8_t byte;
	uint32_t seed;
	int received;
	socklen_t addrlen = sizeof(opp_host);
	
	if ( poll_in(opp_socket, HELLO_TIMEOUT) <= 0 ) {
		log_message(LOG_CONSOLE,
            "Timeout mentre si era in attesa di connessione dall'altro client");
		
		return FALSE;
	}
	
	received = recvfrom(opp_socket, buffer, 5, 0, (struct sockaddr*) &opp_host,
                                                                      &addrlen);
	
	if ( received != 5 ) return FALSE;
	
	unpack(buffer, "bl", &byte, &seed);
	
	print_ip(opp_host);
	flog_message(LOG_DEBUG, "Ricevuto %s da %s:%hu", magic_name(byte), buffer,
                                                      ntohs(opp_host.sin_port));
	
	if ( byte != REQ_HELLO ) return FALSE;
	
	grid.seed = seed;
	update_hash(&grid);
	
	flog_message(LOG_DEBUG, "Il seed per questa partita è %08x", seed);
	
	return TRUE;
}

void get_play_response() {
	uint8_t resp;
	uint16_t opp_udp_port;
	int received;
	
	if ( poll_in(sock_server, PLAY_RESPONSE_TIMEOUT) <= 0 ) {
		log_message(LOG_INFO,
               "Timeout mentre si era in attesa di risposta dall'altro client");
		
		end_match(FALSE);
		return;
	}
	
	if ( recv(sock_server, &resp, 1, 0) != 1 )
		server_disconnected();
	
	switch ( resp ) {
		case RESP_OK_PLAY:
			received = recv(sock_server, buffer, sizeof(struct in_addr) + 2, 0);
			if ( received != sizeof(struct in_addr) + 2 )
				server_disconnected();
			
			opp_host.sin_family = AF_INET;
			memset(opp_host.sin_zero, 0, sizeof(opp_host.sin_zero));
			unpack(buffer, "lw", &(opp_host.sin_addr), &opp_udp_port);
			print_ip(opp_host);
			opp_host.sin_port = htons(opp_udp_port);
			
			flog_message(LOG_INFO, "'%s' ha accettato di giocare con te. "
                                      "Connessione in corso all'host %s:%hu...",
                                            opp_username, buffer, opp_udp_port);
			
			if ( connect_play_socket(&opp_host) && say_hello() ) {
				start_match(GAME_HOST);
				return;
			}
			
			log_message(LOG_INFO, "Impossibile connettersi al client");
			
			end_match(FALSE);
			
			break;
		
		case RESP_REFUSE:
			flog_message(LOG_INFO, "'%s' ha rifiutato la richiesta di gioco",
                                                                  opp_username);
			break;
		
		case RESP_NONEXIST:
			flog_message(LOG_INFO, "'%s' non esiste", opp_username);
			break;
		
		case RESP_BUSY:
			flog_message(LOG_INFO, "'%s' è occupato",
                                                                  opp_username);
			break;
		
		default:
			flog_message(LOG_WARNING, "Risposta dal server inattesa: %s",
                                                              magic_name(resp));
	}
	
	opp_username[0] = '\0';
	my_state = FREE;
}

void list_connected_clients() {
	uint8_t resp, length;
	uint32_t count, i;

	buffer[0] = REQ_WHO;
	
	if ( send_byte(sock_server, REQ_WHO) < 0 ) server_disconnected();
	if ( recv(sock_server, &resp, 1, 0) != 1 ) server_disconnected();
	
	if ( resp != RESP_WHO ) {
		flog_message(LOG_WARNING, "Risposta dal server inattesa: %s",
                                                              magic_name(resp));
		return;
	}
	
	if ( recv(sock_server, buffer, 4, 0) != 4 ) server_disconnected();
	unpack(buffer, "l", &count);
	
	flog_message(LOG_CONSOLE, "Ci sono %u client connessi:", count);
	for (i = 0; i < count; i++) {
		if ( recv(sock_server, &length, 1, 0) != 1 )
			server_disconnected();
		
		if ( recv(sock_server, buffer, length, 0) != length )
			server_disconnected();
		
		buffer[length] = '\0';
		
		if ( strcmp(buffer, my_username) == 0 )
			flog_message(LOG_CONSOLE, "'%s' <-- Sei tu!", buffer);
		else
			flog_message(LOG_CONSOLE, "'%s'", buffer);
	}
}

void make_move(unsigned int cell, bool send_opp) {
	char winner;
	
	grid.cells[cell] = turn;
	update_hash(&grid);
	winner = get_winner(&grid);
	turn = inverse(turn);
	flog_message(LOG_DEBUG, "L'hash è %08x", grid.hash);
	
	if ( send_opp ) {
		pack(buffer, "bbl", REQ_HIT, (uint8_t) cell, grid.hash);
		if ( send_buffer(opp_socket, buffer, 6) < 0 )
			log_error("Errore send()");
	}
	
	if ( turn == my_player )
		flog_message(LOG_INFO, "'%s' ha marcato la casella %d", opp_username, cell);
	else
		flog_message(LOG_CONSOLE, "Hai marcato la casella %d", cell);
	
	
	if ( winner == GAME_UNDEF ) {
		if ( turn == my_player )
			flog_message(LOG_CONSOLE, "E' il tuo turno (%c)", turn);
		else
			flog_message(LOG_CONSOLE, "E' il turno di '%s' (%c)", opp_username, turn);
		
		return;
	}
	
	else if ( winner == my_player ) log_message(LOG_INFO, "Hai vinto!");
	else if ( winner == inverse(my_player) ) log_message(LOG_INFO, "Hai perso!");
	else if ( winner == GAME_DRAW ) log_message(LOG_INFO, "Parità!");
	else return;
	
	/* if ( winner != GAME_UNDEF ) */
	end_match(FALSE);
}

void send_play_request() {
	uint8_t username_length;
	
	username_length = (uint8_t) strlen(opp_username);
	pack(buffer, "bbs", REQ_PLAY, username_length, opp_username);
	if ( send_buffer(sock_server, buffer, 2 + username_length) < 0 )
		server_disconnected();
	
	flog_message(LOG_CONSOLE, "Richiesta di gioco inviata a '%s', in attesa di "
                                                   "risposta...", opp_username);
	
	my_state = BUSY;
}

void server_disconnected() {
	log_message(LOG_ERROR, "Connessione al server interrotta");
	shutdown(sock_server, SHUT_RDWR);
	close(sock_server);
	exit(EXIT_FAILURE);
}

void start_match(char me) {
	my_state = PLAY;
	log_statechange();
	my_player = me;
	turn = GAME_HOST;
	init_grid(&grid);
	monitor_socket_r(opp_socket);
	
	console->prompt = PROMPT_PLAY;
	switch ( me ) {
		case GAME_HOST:
			flog_message(LOG_CONSOLE, "La partita è iniziata. E' il tuo turno "
                                                                  "(%c)", turn);
			
			break;
			
		case GAME_GUEST:
			flog_message(LOG_CONSOLE, "La partita è iniziata. E' il turno di "
                                               "'%s' (%c)", opp_username, turn);
			break;
			
		default:
			flog_message(LOG_WARNING, "Evento inatteso alla linea %d", __LINE__);
	}
}

int poll_in(int socket, int timeout) {
	int poll_ret;
	struct pollfd pfd = {0, POLLIN, 0};
	pfd.fd = socket;
	
	poll_ret = poll(&pfd, 1, timeout);
	
	if ( poll_ret < 0 )
		log_error("Errore poll()");
	else if ( poll_ret > 0 && pfd.revents != POLLIN )
		flog_message(LOG_WARNING | LOG_ERROR, "Evento inatteso alla linea %d",
                                                                      __LINE__);
	return poll_ret;
}
