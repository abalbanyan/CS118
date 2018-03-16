CC=g++
CPPFLAGS=-g -Wall -std=c++11
USERID=304479543_804415450
CLASSES=

all: 
	rm -f server
	rm -f client
	rm -f server_cc
	rm -f client_cc
	make server
	make client
	make server_cc
	make client_cc
server:
	$(CC) -o $@ $(CLASSES) $(CPPFLAGS) $@.cpp rdt_server.cpp

client:
	$(CC) -o $@ $(CLASSES) $(CPPFLAGS) $@.cpp rdt_client.cpp

server_cc:
	$(CC) -o $@ $(CLASSES) $(CPPFLAGS) $@.cpp rdt_server_cc.cpp

client_cc:
	$(CC) -o $@ $(CLASSES) $(CPPFLAGS) $@.cpp rdt_client_cc.cpp

clean:
	rm -rf *.o *~ *.gch *.swp *.dSYM server client client_cc server_cc *.tar.gz

dist: tarball

tarball: clean
	tar -cvzf /tmp/$(USERID).tar.gz --exclude=./.vagrant . && mv /tmp/$(USERID).tar.gz .
