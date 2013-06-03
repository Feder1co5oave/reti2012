CC = gcc
CFLAGS = -Wall -Wextra -pedantic -ansi -MMD -DNDEBUG

SHELL = /bin/bash
TERMINAL = gnome-terminal -x
HOST = 127.0.0.1
PORT = 4096
N = 2

EXEs = tris_client tris_server
SOBJs = client_list.o
COBJs = tris_game.o
COMMONOBJs = common.o log.o set_handler.o
OBJs = $(SOBJs) $(COBJs) $(COMMONOBJs)

.PHONY : all clean server_log client_log run

all : $(EXEs)

-include *.d

tris_server : $(COMMONOBJs) $(SOBJs)

tris_client : $(COMMONOBJs) $(COBJs)

log.o : set_handler.o

set_handler.o :
	$(CC) $(CFLAGS) -D_POSIX_SOURCE -c -o set_handler.o set_handler.c

clean :
	- rm $(EXEs) *.o logs/*.log *.d

server_log :
	pgrep tris_server > /dev/null
	tail -f --lines=20 --pid=`pgrep tris_server` logs/tris_server.log

client_log :
	pgrep tris_client > /dev/null
	pgrep tris_client | sed 's| *\([0-9][0-9]*\)|logs/tris_client-\1.log|' | xargs tail -f --lines=20 --pid=`pgrep tris_client -o`

run : all
	$(TERMINAL) ./tris_server $(HOST) $(PORT)
	$(TERMINAL) $(MAKE) server_log
	for (( i=0; i<$(N); i++ )); do $(TERMINAL) ./tris_client $(HOST) $(PORT); done
	$(TERMINAL) $(MAKE) client_log
