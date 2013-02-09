CC = gcc
CFLAGS = -Wall -Wextra -pedantic -ansi -MMD -DNDEBUG

TERMINAL = gnome-terminal -x
HOST = 127.0.0.1
PORT = 4096

EXEs = tris_client tris_server
SOBJs = client_list.o
COBJs = tris_game.o
COMMONOBJs = common.o pack.o log.o set_handler.o
OBJs = $(SOBJs) $(COBJs) $(COMMONOBJs)
LOCALE = it

.PHONY : all clean server_log client_log run

all : $(EXEs) locale/$(LOCALE)/LC_MESSAGES/tris.mo

-include *.d

tris_server : $(COMMONOBJs) $(SOBJs)

tris_client : $(COMMONOBJs) $(COBJs)

log.o : set_handler.o

set_handler.o :
	$(CC) -Wall -Wextra -pedantic       -MMD -DNDEBUG   -c -o set_handler.o set_handler.c

tris.pot : *.c *.h
	xgettext -k_ -d tris --from-code=UTF-8 --no-location -s -o tris.pot $^

locale/$(LOCALE).po : tris.pot
	msgmerge -s -v --force-po --backup=off -U $@ $^
	touch -m $@

locale/%/LC_MESSAGES/tris.mo : locale/%.po
	mkdir -p $(dir $@)
	msgfmt -c -v -o $@ $^

clean :
	- rm $(OBJs) $(EXEs) logs/*.log *.d

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
