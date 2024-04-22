TARGETS = kvstore master
CXXFLAGS = -w

all: $(TARGETS)

kvstore: kvstore.cc helper.h
	g++ $(CXXFLAGS) $^ -pthread -o $@

master: master.cc helper.h
	g++ $(CXXFLAGS) $^ -pthread -o $@

clean:
	rm -fv $(TARGETS) *~ *.o
