TARGETS = kvstore master

all: $(TARGETS)

kvstore: kvstore.cc helper.h
	g++ $^ -pthread -o $@

master: master.cc helper.h
	g++ $^ -pthread -o $@

clean::
	rm -fv $(TARGETS) *~ *.o