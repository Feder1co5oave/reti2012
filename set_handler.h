#ifndef _SET_HANDLER_H
#define _SET_HANDLER_H

/**
 * Set a signal handler. Must compile with -D_POSIX_SOURCE.
 * @see man 2 sigaction for details.
 * @param int signal the signal to handle
 * @param void (*handler)(int) the handler function
 * @return int zero on success, -1 on error (@see sigaction())
 */
int set_handler(int signal, void (*handler)(int));

#endif