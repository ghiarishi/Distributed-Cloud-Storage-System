TARGETS = kvstore master test
CXXFLAGS = -w

all: $(TARGETS)

kvstore: kvstore.cc helper.h
	g++ $(CXXFLAGS) $^ -pthread -o $@

master: master.cc helper.h
	g++ $(CXXFLAGS) $^ -pthread -o $@

test: test.cc
	g++ $(CXXFLAGS) $^ -pthread -o $@

clean:
	rm -fv $(TARGETS) *~ *.o
