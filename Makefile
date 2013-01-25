CC = gcc
CFLAGS = -Wall -Wextra -pedantic -ansi -MMD

EXEs = tris_client tris_server
SOBJs = client_list.o
COBJs = tris_game.o
COMMONOBJs = common.o pack.o log.o set_handler.o
OBJs = $(SOBJs) $(COBJs) $(COMMONOBJs)
LOCALE = it

.PHONY : all clean server_log

all : $(EXEs) locale/$(LOCALE)/LC_MESSAGES/tris.mo

-include *.d
	
tris_server : $(COMMONOBJs) $(SOBJs)

tris_client : $(COMMONOBJs) $(COBJs)

log.o : set_handler.o

set_handler.o :
	$(CC) -Wall -Wextra -pedantic       -MMD   -c -o set_handler.o set_handler.c

tris.pot : *.c *.h
	xgettext -k_ -d tris --from-code=UTF-8 --no-location -s -o tris.pot $^

locale/$(LOCALE).po : tris.pot
	msgmerge -s -v --force-po --backup=off -U $@ $^
	touch -m $@

locale/%/LC_MESSAGES/tris.mo : locale/%.po
	mkdir -p $(dir $@)
	msgfmt -c -v -o $@ $^

clean :
	- rm $(OBJs) $(EXEs) *.log *.d
	- rm -r tris.pot locale/*/

server_log :
	ps -C tris_server -o pid= > /dev/null
	tail -f --lines=20 "--pid=$(shell ps -C tris_server -o pid=)" tris_server.log
