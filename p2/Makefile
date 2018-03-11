CC=g++
CPPFLAGS=-g -Wall -std=c++11
USERID=304479543
CLASSES=

all: 
	rm server
	rm client
	make server
	make client
server:
	$(CC) -o $@ $(CLASSES) $(CPPFLAGS) $@.cpp rdt_server.cpp

client:
	$(CC) -o $@ $(CLASSES) $(CPPFLAGS) $@.cpp rdt_client.cpp

clean:
	rm -rf *.o *~ *.gch *.swp *.dSYM server *.tar.gz

dist: tarball

tarball: clean
	tar -cvzf /tmp/$(USERID).tar.gz --exclude=./.vagrant . && mv /tmp/$(USERID).tar.gz .
