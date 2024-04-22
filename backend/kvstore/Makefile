TARGETS = kvstore

all: $(TARGETS)

kvstore: kvstore.cc
	g++ $^ -pthread -o $@

clean::
	rm -fv $(TARGETS) *~ *.o