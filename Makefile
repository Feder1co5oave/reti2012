CC = gcc
CFLAGS = -Wall -MMD

EXEs = tris_client tris_server
SOBJs = client_list.o
COBJs =
COMMONOBJs = common.o pack.o log.o
OBJs = $(SOBJs) $(COBJs) $(COMMONOBJs)

.PHONY : all clean server_log

all : $(EXEs)

-include tris_client.d tris_server.d
-include $(OBJs:.o=.d)
	
tris_server : $(COMMONOBJs) $(SOBJs)

tris_client : $(COMMONOBJs) $(COBJs)


clean :
	- rm $(OBJs) $(EXEs) *.log *.d

server_log :
	ps -C tris_server -o pid= > /dev/null
	tail -f --lines=20 "--pid=$(shell ps -C tris_server -o pid=)" tris_server.log
