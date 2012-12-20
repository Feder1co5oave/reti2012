CC = gcc
CFLAGS = -Wall -pedantic

EXEs = tris_client tris_server
SOBJs = client_list.o
COBJs =
COMMONOBJs = common.o pack.o
OBJs = $(SOBJs) $(COBJs) $(COMMONOBJs)

.PHONY : all clean

all : $(EXEs)
	
tris_server : $(COMMONOBJs) $(SOBJs)

tris_client : $(COMMONOBJs) $(COBJs)

%.o : %.c %.h

clean :
	- rm $(OBJs) $(EXEs)
