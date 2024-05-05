# Compiler
#CC = g++

# Compiler flags
#CFLAGS = -std=c++11 -pthread

# Executable name
#TARGET = frontendserver

# Source files
#SRCS = frontendserver.cc

# Rule to build the executable
#$(TARGET): $(SRCS)
#	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)

# Clean rule
#clean:
#	rm -f $(TARGET)

TARGETS = frontendserver loadbalancer

all: $(TARGETS)

frontendserver: frontendserver.cc
	g++ $^ -lpthread -g -lssl -lcrypto -o $@

loadbalancer: loadbalancer.cc
	g++ $^ -lpthread -g -lssl -lcrypto -o $@

clean::
	rm -fv $(TARGETS) *~
