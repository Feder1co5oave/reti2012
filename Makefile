CC = gcc
CFLAGS = -Wall -Wextra -pedantic -ansi -MMD

EXEs = tris_client tris_server
SOBJs = client_list.o
COBJs =
COMMONOBJs = common.o pack.o log.o
OBJs = $(SOBJs) $(COBJs) $(COMMONOBJs)
LOCALE = it

.PHONY : all clean server_log

all : $(EXEs) locale/$(LOCALE)/LC_MESSAGES/tris.mo

-include tris_client.d tris_server.d
-include $(OBJs:.o=.d)
	
tris_server : $(COMMONOBJs) $(SOBJs)

tris_client : $(COMMONOBJs) $(COBJs)


tris.pot : *.c *.h
	xgettext -k_ -d tris --from-code=UTF-8 -s -o tris.pot $^

locale/$(LOCALE).po : tris.pot
	msgmerge -s -v --force-po -U $@ $^
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
