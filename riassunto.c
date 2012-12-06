// SERVER ======================================================================
int sock_listen, sock_connect
int yes = 1
struct addrinfo hints
struct addrinfo *gai_results, *p
struct sockaddr_storage	client_addr;

memset(&hints, 0)
hints.ai_family = AF_INET | AF_INET6 | AF_UNSPEC
hints.ai_socktype = SOCK_STREAM | SOCK_DGRAM
hints.ai_flags = AI_PASSIVE
getaddrinfo(NULL, PORT, &hints, &gai_results)
p = gai_results
freeaddrinfo(gai_results)
sock_listen = socket(p->ai_family, p->ai_socktype, p->ai_protocol)
setsockopt(sock_listen, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int))
bind(sock_listen, p->ai_addr, p->ai_addrlen)
listen(sock_listen, BACKLOG)
sin_size = sizeof(struct sockaddr_storage)
sock_connect = accept(sock_listen, (struct sockaddr *)&client_addr, &sin_size)
send(sock_connect, "Hello world", 13, 0)
close(sock_connect)
close(sock_listen)

// CLIENT ======================================================================
int sock;
char buf[MAXSIZE];
struct addrinfo hints, *gai_results, *p;

memset(&hints, 0);
hints.ai_family = AF_INET | AF_INET6 | AF_UNSPEC
hints.ai_socktype = SOCK_STREAM | SOCK_DGRAM
getaddrinfo(NULL, PORT, &hints, &gai_results)
p = gai_results
freeaddrinfo(gai_results)
sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol)
connect(sock, p->ai_addr, p->ai_addrlen)
recv(sock, buf, MAXSIZE - 1, 0)
close(sock)

/**
 * Un indirizzo generale
 */
struct sockaddr {
    unsigned short    sa_family;    // address family, AF_xxx
    char              sa_data[14];  // 14 bytes of protocol address
}; 

/**
 * Un indirizzo socket IPv4
 */
struct sockaddr_in {
    short int          sin_family;  // Address family, AF_INET
    unsigned short int sin_port;    // Port number
    struct in_addr     sin_addr;    // Internet address
    unsigned char      sin_zero[8]; // Same size as struct sockaddr
};
struct in_addr {
    uint32_t s_addr; // that's a 32-bit int (4 bytes)
};

/**
 * Un indirizzo socket IPv6
 */
struct sockaddr_in6 {
    u_int16_t       sin6_family;   // address family, AF_INET6
    u_int16_t       sin6_port;     // port number, Network Byte Order
    u_int32_t       sin6_flowinfo; // IPv6 flow information
    struct in6_addr sin6_addr;     // IPv6 address
    u_int32_t       sin6_scope_id; // Scope ID
};
struct in6_addr {
    unsigned char   s6_addr[16];   // IPv6 address
};

/**
 * Tiene diversi tipi di indirizzi, non va manipolata direttamente a mano
 */
struct sockaddr_storage {
    sa_family_t  ss_family;     // address family
    // all this is padding, implementation specific, ignore it:
    char      __ss_pad1[_SS_PAD1SIZE];
    int64_t   __ss_align;
    char      __ss_pad2[_SS_PAD2SIZE];
};

/**
 * Prepara un socket
 * @param int address_family AF_INET o AF_INET6
 * @param int type SOCK_STREAM o SOCK_DGRAM
 * @param int protocol tcp o udp (0 per farlo scegliere automaticamente, o usare getprotobyname())
 * @return int un socket descriptor o -1 su errore (setta errno)
 */
int socket(int address_family, int type, int protocol);

/**
 * Setta un'opzione sul socket per fare in modo che l'indirizzo sia riusabile
 */
int yes=1;
if (setsockopt(sock_listen, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
    perror("setsockopt");
    exit(1);
}

/**
 * Collega un socket su una specifica porta su uno specifico indirizzo IPv4
 * @param int socket il socket già creato con socket()
 * @param struct sockaddr *my_addr l'indirizzo ip/porta da bindare; si può usare addrinfo.ai_addr o un sockaddr_in(6)
 * @param int addrlen la lunghezza dell'indirizzo; si può usare addrinfo.ai_addrlen oppure sizeof(sockaddr_in(6))
 * @return int -1 su errore (setta errno)
 */
int bind(int socket, struct sockaddr *my_addr, int addrlen);

/**
 * Si connette a un listening socket su un server.
 * @param int socket il socket client già creato con socket()
 * @param struct sockaddr *server_addr l'indirizzo ip/porta del listening socket
 * @param int addrlen la lunghezza dell'indirizzo, vedi bind()
 */
int connect(int socket, struct sockaddr *server_addr, int addrlen);

/**
 * Pone in ascolto un socket già bind()ato.
 * @param int socket
 * @param int backlog il numero di richieste in entrata che si possono mettere in coda (<= 20)
 */
int listen(int socket, int backlog);

/**
 * Accetta una connessione in entrata da un listening socket.
 * @param int socket un socket già bind()ato e listen()ing
 * @param struct sockaddr *addr l'indirizzo di una struct sockaddr_storage che avrà l'indirizzo ip/porta del client
 * @param socklen_t *addrlen l'indirizzo di una variabile che contiene la lunghezza di addr (di solito si setta a sizeof(struct sockaddr_storage))
 * @return int un *nuovo* socket descriptor per la comunicazione al client o -1 su errore (setta errno)
 */
int accept(int socket, struct sockaddr *addr, socklen_t *addrlen);

/**
 * Invia dati attraverso un socket già aperto con connect() o accept().
 * Se il socket è SOCK_DGRAM si deve usare sendto() oppure effettuare prima una connect().
 * @param int socket il socket su cui inviare dati
 * @param const void *msg il buffer contenente i dati da inviare
 * @param int len la lughezza del messaggio da inviare
 * @param int flags di solito si setta a 0
 * @return int il numero di byte inviati (<= len) se è ok, -1 su errore (setta errno)
 */
int send(int socket, const void *msg, int len, int flags);

/**
 * Ricevi dati da un socket già aperto con connect() o accept().
 * Se il socket è SOCK_DGRAM si deve usare recvfrom() oppure effettuare prima una connect().
 * @param int socket il socket da cui ricevere i dati
 * @param void *buf il buffer su cui verranno scritti i dati ricevuti
 * @param int len la dimensione del buffer, cioè il numero *massimo* di byte da ricevere
 * @param int flags di solito si setta a 0 (o MSG_WAIALL se si vuole aspettare che arrivino *tutti* i len byte)
 * @return int il numero di byte effettivamente ricevuti (<= len) se è ok, 0 se l'host remoto ha chiuso la connessione, -1 su errore (setta errno)
 */
int recv(int socket, void *buf, int len, int flags);

/**
 * Chiudi un descrittore di socket.
 * @param in socket il descrittore da chiudere
 * @return int 0 se ok, -1 su errore (setta errno)
 */
int close(int socket);

/**
 * Disabilita ogni successiva send/recv su un descrittore. Il descrittore rimane comunque aperto e va chiuso con close().
 * @param int socket
 * @param int how SHUT_RD, SHUT_WR o SHUT_RDWR rispettivamente per disabilitare recv, send o entrambe
 */
int shutdown(int socket, int how);

/**
 * Host-to-Network o Network-to-Host byte order
 */
uint32_t htonl(uint32_t hostlong);
uint16_t htons(uint16_t hostshort);
uint32_t ntohl(uint32_t netlong);
uint16_t ntohs(uint16_t netshort);

/**
 * Presentation-to-Network (printable-to-numeric): converte un indirizzo IP contenuto in una stringa
 * nella rappresentazione a bit.
 * @param int addr_family AF_INET o AF_INET6
 * @param const char *str la stringa contenente l'indirizzo
 * @param void *dst l'indirizzo della struttura dati che conterrà l'IP, di solito una in(6)_addr
 * @return 1 se ok, -1 su errore (setta errno) o 0 se l'indirizzo IP non è valido
 */
int inet_pton(int addr_family, const char *str, void *dst);

/**
 * Network-to-Presentation (numeric-to-printable): converte un indirizzo IP contenuto in una struttura
 * in una rappresentazione in stringa.
 * @param int addr_family AF_INET o AF_INET6
 * @param const void *src la struttura che contiene l'IP, di solito una in(6)_addr
 * @param char *dst il buffer che conterrà la stringa con la rappresentazione
 * @param int size la lunghezza del buffer, di solito INET_ADDRSTRLEN o INET6_ADDRSTRLEN
 * @return const char* l'indirizzo del buffer se ok, NULL su errore (setta errno)
 */
const char *inet_ntop(int addr_family, const void *src, char *dst, int size);

/**
 * Moderna struttura per costruire i socket
 */
struct addrinfo {
    int              ai_flags;     // AI_PASSIVE, AI_CANONNAME, etc.
    int              ai_family;    // AF_INET, AF_INET6, AF_UNSPEC
    int              ai_socktype;  // SOCK_STREAM, SOCK_DGRAM
    int              ai_protocol;  // use 0 for "any"
    size_t           ai_addrlen;   // size of ai_addr in bytes
    struct sockaddr *ai_addr;      // struct sockaddr_in or _in6
    char            *ai_canonname; // full canonical hostname
    struct addrinfo *ai_next;      // linked list, next node
};

/**
 * Funzione di utilità generale, risolve nomi DNS e nomi di servizi.
 * Solitamente si compila una struct addrinfo con alcune informazioni, dopo averla azzerata completamente con memset().
 * In ai_flags si mette AI_PASSIVE per lasciare vuoto l'indirizzo IP, che verrà riempito poi automaticamente con quello dell'host corrente.
 * @param const char *hostname il nome dell'host, cioè un indirizzo IP o un nome DNS, NULL se si usa hints con AI_PASSIVE
 * @param const char *port una stringa contenente un numero di porta o il nome di un servizio che sarà tradotto usando /etc/services
 * @param const struct addrinfo *hints contiene dei dati parziali che la funzione cercherà di riempire
 * @param struct addrinfo **res un puntatore passato per indirizzo, che conterrà l'indirizzo di una linkedlist di addrinfo contenenti i risultati trovati
 * @return int 0 se ok, un codice di errore nonzero su errore - vedi gai_strerror()
 */
int getaddrinfo(const char *hostname, const char *port, const struct addrinfo *hints, struct addrinfo **res);

/**
 * Va chiamata dopo getaddrinfo per deallocare la linkedlist restituita tramite res.
 */
void freeaddrinfo(struct addrinfo *res);

/**
 * Restituisce una descrizione testuale dell'errore prodotto da getaddrinfo().
 * @param int code il codice di errore restituito da getaddrinfo()
 * @return la stringa di errore
 */
const char *gai_strerror(int code)

/**
 * Macro di manipolazione per i set di descrittori da dare alla select().
 */
FD_SET(int descriptor, fd_set *set);
FD_CLR(int descriptor, fd_set *set);
FD_ISSET(int descriptor, fd_set *set);
FD_ZERO(fd_set *set);

/**
 * Aspetta fino a quando uno degli stream inseriti in readfds/writefds/exceptionfds non
 * è pronto a ricevere/inviare dati o c'è un errore, oppure fino a quando non scade il timeout.
 * Al ritorno, se >0, i set sono stati modificati e sono alzati solo i flag dei descrittori che sono pronti.
 * Se è pronto un descrittore in readfds, la recv() sarà "non bloccante".
 * @param int max_n il descrittore di valore massimo presente nei set (permette di restringere la ricerca)
 * @param fd_set *readfds il set dei descrittori su cui voglio attendere che arrivino dati
 * @param fd_set *writefds il set di descrittori su cui voglio attendere che il buffer di uscita sia non pieno
 * @param fd_set *exceptionfds il set di descrittori su cui voglio attendere per un errore
 * @param struct timeval *timeout il tempo utile per attivare uno dei descrittori. NULL = mai
 * @return int >0 se ok (i set sono stati modificati), 0 se è scaduto il timeout, -1 su errore (setta errno)
 */
int select (int max_n, fd_set *readfds, fd_set *writefds, fd_set *exceptionfds, struct timeval *timeout);

struct timeval {
	time_t      tv_sec;
	suseconds_t tv_usec;
};