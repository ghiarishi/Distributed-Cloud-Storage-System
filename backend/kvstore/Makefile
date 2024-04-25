TARGETS = kvstore master test1 test2 test3
CXXFLAGS = -w

all: $(TARGETS)

kvstore: kvstore.cc helper.h
	g++ $(CXXFLAGS) $^ -pthread -o $@

master: master.cc helper.h
	g++ $(CXXFLAGS) $^ -pthread -o $@

test1: test1.cc
	g++ $(CXXFLAGS) $^ -pthread -o $@

test2: test2.cc
	g++ $(CXXFLAGS) $^ -pthread -o $@

test3: test3.cc
	g++ $(CXXFLAGS) $^ -pthread -o $@

clean:
	rm -fv $(TARGETS) *~ *.o
