CC = gcc
CFLAGS = -Wall -Wextra -pedantic -ansi -MMD -DNDEBUG

TERMINAL = gnome-terminal -x
HOST = 127.0.0.1
PORT = 4096

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
	ps -C tris_server -o pid= > /dev/null
	tail -f --lines=20 "--pid=$(shell ps -C tris_server -o pid=)" logs/tris_server.log

client_log :
	ps -C tris_client -o pid= > /dev/null
	ps -C tris_client -o pid= | sed 's| *\([0-9][0-9]*\)|logs/tris_client-\1.log|' | xargs tail -f --lines=20

run :
	$(TERMINAL) ./tris_server $(HOST) $(PORT)
	$(TERMINAL) make server_log
	$(TERMINAL) ./tris_client $(HOST) $(PORT)
	$(TERMINAL) ./tris_client $(HOST) $(PORT)
	$(TERMINAL) make client_log
