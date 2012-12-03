CC = gcc
CFLAGS = -Wall -pedantic

OBJS = pack.o client_list.o tris_client tris_server

.PHONY : all clean

all : tris_server tris_client
	
tris_server tris_client : pack.o client_list.o

clean :
	- rm $(OBJS)
