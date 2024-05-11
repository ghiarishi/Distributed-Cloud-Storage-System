TARGETS = frontendserver loadbalancer admin kvstore master smtpServer smtpec
CXXFLAGS = -w -I/usr/include/openssl
LDFLAGS = -pthread -lssl -lcrypto

FRONTENDSERVER_SRCS = frontend/frontendserver.cc frontend/render.cc frontend/readingHelper.cc frontend/emailHelper.cc frontend/loginHelper.cc
FRONTENDSERVER_OBJS = $(FRONTENDSERVER_SRCS:.cc=.o)

all: $(TARGETS)

frontendserver: $(FRONTENDSERVER_OBJS)
	g++ $(FRONTENDSERVER_OBJS) $(CXXFLAGS) $(LDFLAGS) -lpthread -g -o frontend/$@

loadbalancer: frontend/loadbalancer.cc $(OBJECTS)
	g++ $< $(CXXFLAGS) $(LDFLAGS) -lpthread -g -o frontend/$@

admin: frontend/admin.cc $(OBJECTS)
	g++ $< $(CXXFLAGS) $(LDFLAGS) -lpthread -g -o frontend/$@

clean:
	rm -fv frontend/$(TARGETS) backend/kvstore/$(TARGETS) smtp/$(TARGETS) $(OBJECTS) *~

kvstore: backend/kvstore/kvstore.cc backend/kvstore/helper.h
	g++ $< $(CXXFLAGS) -o backend/kvstore/$@ $(LDFLAGS)

master: backend/kvstore/master.cc backend/kvstore/helper.h
	g++ $< $(CXXFLAGS) -o backend/kvstore/$@ $(LDFLAGS)

smtpServer: smtp/smtpServer.cc
	g++ $< $(CXXFLAGS) $(LDFLAGS) -lpthread -g -o smtp/$@

smtpec: smtp/smtpec.cc
	g++ $< $(CXXFLAGS) $(LDFLAGS) -lpthread -lresolv -g -o smtp/$@
