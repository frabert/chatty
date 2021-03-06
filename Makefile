#
# chatterbox Progetto del corso di LSO 2017/2018 
# 
# Dipartimento di Informatica Università di Pisa
# Docenti: Prencipe, Torquati
#
#

##########################################################
# IMPORTANTE: completare la lista dei file da consegnare
# 
FILE_DA_CONSEGNARE=Makefile *.h *.c relazione/relazione.tex \
			DATA/chatty.conf1 DATA/chatty.conf2 Doxyfile doxygen.pdf relazione.pdf
# inserire il nome del tarball: es. NinoBixio
TARNAME=FrancescoBertolaccini
# inserire il corso di appartenenza: CorsoA oppure CorsoB
CORSO=CorsoB
#
# inserire l'email sulla quale ricevere comunicazione sul progetto
# e per sostenere l'esame
MAIL=bertolaccinifrancesco@gmail.com
#
###########################################################

###################################################################
# NOTA: Il nome riportato in UNIX_PATH deve corrispondere al nome 
#       usato per l'opzione UnixPath nel file di configurazione del 
#       server (vedere i file nella directory DATA).
#       Lo stesso vale per il nome riportato in STAT_PATH e DIR_PATH
#       che deveno corrispondere con l'opzione StatFileName e 
#       DirName, rispettivamente.
#
# ATTENZIONE: se il codice viene sviluppato sulle macchine del 
#             laboratorio utilizzare come nomi, nomi unici, 
#             ad esempo /tmp/chatty_sock_<numero-di-matricola> e
#             /tmp/chatty_stats_<numero-di-matricola>.
#
###################################################################
UNIX_PATH       = /tmp/chatty_socket
STAT_PATH       = /tmp/chatty_stats.txt
DIR_PATH        = /tmp/chatty

CC		=  gcc
AR              =  ar
CFLAGS	        += -std=c99 -Wall -pedantic -g -DMAKE_VALGRIND_HAPPY -DCSTRLIST_POSIX_COMPLIANT
ARFLAGS         =  rvs
INCLUDES	= -I.
LDFLAGS 	= -L.
OPTFLAGS	= #-O3 
LIBS            = -pthread -lcfgparse -lcqueue -lchash -lccircbuf -lcstrlist

# aggiungere qui altri targets se necessario
TARGETS		= chatty        \
		  client

# aggiungere qui i file oggetto da compilare
OBJECTS		= chatty_handlers.o chatty.o libcfgparse.a libcqueue.a libchash.a libccircbuf.a libcstrlist.a connections.o

# aggiungere qui gli altri include 
INCLUDE_FILES   = connections.h \
		  message.h     \
		  ops.h	  	\
		  stats.h       \
		  config.h \
		  cfgparse.h

.PHONY: all clean cleanall test1 test2 test3 test4 test5 consegna memcheck docs relazione extra_tests
.SUFFIXES: .c .h

%: %.c
	$(CC) $(CFLAGS) $(INCLUDES) $(OPTFLAGS) -o $@ $< $(LDFLAGS) 

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) $(OPTFLAGS) -c -o $@ $<

all: $(TARGETS)

ccircbuf_tests: ccircbuf_tests.o libccircbuf.a
	$(CC) $(CFLAGS) $(INCLUDES) $(OPTFLAGS) $(LDFLAGS) -o $@ $^ -pthread -lccircbuf

cfgparse_tests: cfgparse_tests.o libcfgparse.a
	$(CC) $(CFLAGS) $(INCLUDES) $(OPTFLAGS) $(LDFLAGS) -o $@ $^ -pthread -lcfgparse

chash_tests: chash_tests.o libchash.a
	$(CC) $(CFLAGS) $(INCLUDES) $(OPTFLAGS) $(LDFLAGS) -o $@ $^ -pthread -lchash

extra_tests: ccircbuf_tests cfgparse_tests chash_tests
	./ccircbuf_tests
	./cfgparse_tests
	./chash_tests
	echo "Test aggiuntivi svolti con successo"

docs:
	doxygen
	$(MAKE) -C doc/latex
	mv doc/latex/refman.pdf doxygen.pdf

relazione:
	$(MAKE) -C relazione

# Test valgrind
memcheck: chatty
	\mkdir -p $(DIR_PATH)
	valgrind -v --show-leak-kinds=all --leak-check=full --track-origins=yes ./chatty -f DATA/chatty.conf1

chatty: chatty.o chatty_handlers.o libchatty.a $(INCLUDE_FILES)
	$(CC) $(CFLAGS) $(INCLUDES) $(OPTFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

client: client.o connections.o message.h
	$(CC) $(CFLAGS) $(INCLUDES) $(OPTFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

libcfgparse.a: cfgparse.o
	$(AR) $(ARFLAGS) $@ $^

libcqueue.a: cqueue.o
	$(AR) $(ARFLAGS) $@ $^

libchash.a: chash.o
	$(AR) $(ARFLAGS) $@ $^

libccircbuf.a: ccircbuf.o
	$(AR) $(ARFLAGS) $@ $^

libcstrlist.a: cstrlist.o
	$(AR) $(ARFLAGS) $@ $^

# test gruppi
test6:
	make cleanall
	\mkdir -p $(DIR_PATH)
	make all
	./chatty -f DATA/chatty.conf1&
	./testgroups.sh $(UNIX_PATH)
	killall -QUIT -w chatty
	@echo "********** Test6 superato!"

############################ non modificare da qui in poi

libchatty.a: $(OBJECTS)
	$(AR) $(ARFLAGS) $@ $^

clean		: 
	rm -f $(TARGETS)
	rm -f ./*_tests

cleanall	: clean
	-killall -KILL -w chatty -w client
	\rm -f *.o *~ *.a valgrind_out $(STAT_PATH) $(UNIX_PATH)
	\rm -fr  $(DIR_PATH)

killchatty:
	killall -9 chatty

# test base
test1: 
	make cleanall
	\mkdir -p $(DIR_PATH)
	make all
	./chatty -f DATA/chatty.conf1&
	./client -l $(UNIX_PATH) -c pippo
	./client -l $(UNIX_PATH) -c pluto
	./client -l $(UNIX_PATH) -c minni
	./client -l $(UNIX_PATH) -k pippo -S "Ciao pluto":pluto -S "come stai?":pluto
	./client -l $(UNIX_PATH) -k pluto -p -S "Ciao pippo":pippo -S "bene e tu?":pippo -S "Ciao minni come stai?":minni
	./client -l $(UNIX_PATH) -k pippo -p
	./client -l $(UNIX_PATH) -k pluto -p
	./client -l $(UNIX_PATH) -k minni -p
	killall -QUIT -w chatty
	@echo "********** Test1 superato!"

# test scambio file 
test2:
	make cleanall
	\mkdir -p $(DIR_PATH)
	make all
	./chatty -f DATA/chatty.conf1&
	./testfile.sh $(UNIX_PATH) $(DIR_PATH)
	killall -QUIT -w chatty
	@echo "********** Test2 superato!"

# test parametri di configurazione e statistiche
test3:
	make cleanall
	\mkdir -p $(DIR_PATH)
	make all
	./chatty -f DATA/chatty.conf2&
	./testconf.sh $(UNIX_PATH) $(STAT_PATH)
	killall -QUIT -w chatty
	@echo "********** Test3 superato!"


# verifica di memory leaks 
test4:
	make cleanall
	\mkdir -p $(DIR_PATH)
	make all
	./testleaks.sh $(UNIX_PATH)
	@echo "********** Test4 superato!"

# stress test
test5:
	make cleanall
	\mkdir -p $(DIR_PATH)
	make all
	./chatty -f DATA/chatty.conf1&
	./teststress.sh $(UNIX_PATH)
	killall -QUIT -w chatty
	@echo "********** Test5 superato!"

# target per la consegna
consegna: relazione docs
	make test1
	sleep 3
	make test2
	sleep 3
	make test3
	sleep 3
	make test4
	sleep 3
	make test5
	sleep 3
	make test6
	sleep 3
	tar -cvf $(TARNAME)_$(CORSO)_chatty.tar $(FILE_DA_CONSEGNARE) 
	@echo "*** TAR PRONTO $(TARNAME)_$(CORSO)_chatty.tar "
	@echo "Per la consegna seguire le istruzioni specificate nella pagina del progetto:"
	@echo " http://didawiki.di.unipi.it/doku.php/informatica/sol/laboratorio17/progetto"
	@echo 
