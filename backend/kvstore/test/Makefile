TARGETS = test1 test2 test3
CXXFLAGS = -w
LDFLAGS = -pthread -lssl -lcrypto

all: $(TARGETS)

test1: test1.cc
	g++ $(CXXFLAGS) test1.cc -o $@

test2: test2.cc
	g++ $(CXXFLAGS) test2.cc -o $@

test3: test3.cc
	g++ $(CXXFLAGS) test3.cc -o $@

clean:
	rm -fv $(TARGETS) *~ *.o
