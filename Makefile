CC = gcc
CFLAGS = -Wall -Wextra -pedantic -ansi -MMD

EXEs = tris_client tris_server
SOBJs = client_list.o
COBJs =
COMMONOBJs = common.o pack.o log.o set_handler.o
OBJs = $(SOBJs) $(COBJs) $(COMMONOBJs)

.PHONY : all clean server_log

all : $(EXEs)

-include *.d
	
tris_server : $(COMMONOBJs) $(SOBJs)

tris_client : $(COMMONOBJs) $(COBJs)

log.o : set_handler.o

set_handler.o :
	$(CC) -Wall -Wextra -pedantic       -MMD   -c -o set_handler.o set_handler.c

clean :
	- rm $(OBJs) $(EXEs) *.log *.d

server_log :
	ps -C tris_server -o pid= > /dev/null
	tail -f --lines=20 "--pid=$(shell ps -C tris_server -o pid=)" tris_server.log
