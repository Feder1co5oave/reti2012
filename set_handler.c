#include "set_handler.h"

int set_handler(int signal, void (*handler)(int)) {
	struct sigaction sa;
	
	sa.sa_handler = handler;
	sigfillset(&sa.sa_mask);
	sa.sa_flags = 0;
	
	return sigaction(signal, &sa, NULL);
}
