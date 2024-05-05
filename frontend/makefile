TARGETS = frontendserver loadbalancer admin

all: $(TARGETS)

frontendserver: frontendserver.cc $(OBJECTS)
	g++ $^ -lpthread -g -lssl -lcrypto -o $@

loadbalancer: loadbalancer.cc $(OBJECTS)
	g++ $^ -lpthread -g -lssl -lcrypto -o $@

admin: admin.cc $(OBJECTS)
	g++ $^ -lpthread -g -lssl -lcrypto -o $@

clean::
	rm -fv $(TARGETS) $(OBJECTS) *~
