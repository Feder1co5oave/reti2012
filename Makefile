CC = gcc
CFLAGS = -Wall -pedantic

EXEs = tris_client tris_server
SOBJs = client_list.o
COBJs =
COMMONOBJs = common.o pack.o log.o
OBJs = $(SOBJs) $(COBJs) $(COMMONOBJs)

.PHONY : all clean server_log

all : $(EXEs)
	
tris_server : $(COMMONOBJs) $(SOBJs)

tris_client : $(COMMONOBJs) $(COBJs)

%.o : %.c %.h

clean :
	- rm $(OBJs) $(EXEs) *.log

server_log :
	ps -C tris_server -o pid= > /dev/null
	tail -f --lines=20 "--pid=$(shell ps -C tris_server -o pid=)" tris_server.log
