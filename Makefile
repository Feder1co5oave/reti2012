CC = gcc
CFLAGS = -Wall -pedantic

.PHONY : all

all : tris_server tris_client
	
tris_server tris_client : pack.o
