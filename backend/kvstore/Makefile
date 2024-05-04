TARGETS = kvstore master
CXXFLAGS = -w
LDFLAGS = -pthread -lssl -lcrypto

all: $(TARGETS)

kvstore: kvstore.cc helper.h
	g++ $(CXXFLAGS) kvstore.cc -o $@ $(LDFLAGS)

master: master.cc helper.h
	g++ $(CXXFLAGS) master.cc -o $@ $(LDFLAGS)

clean:
	rm -fv $(TARGETS) *~ *.o
